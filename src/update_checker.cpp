#include "update_checker.h"
#include "settings.h"
#include "soundbridge_version.h"
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <array>
#include <charconv>
#include <chrono>
#include <optional>
#include <sstream>

namespace soundbridge {
namespace {
std::optional<std::array<unsigned, 3>> versionParts(const std::string& text) {
    std::array<unsigned, 3> result{};
    std::size_t offset = !text.empty() && text.front() == 'v' ? 1 : 0;
    for (int i = 0; i < 3; ++i) {
        const auto end = i == 2 ? text.size() : text.find('.', offset);
        if (end == std::string::npos || end == offset || (end - offset > 1 && text[offset] == '0')) return {};
        const auto parsed = std::from_chars(text.data() + offset, text.data() + end, result[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + end) return {};
        offset = end + 1;
    }
    return result;
}
UpdateResult failure(const Message& text) { return {UpdatePhase::Error, text, {}, {}}; }
std::uint64_t unixTime() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
struct InternetHandle {
    HINTERNET value;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};
}
bool newerVersion(const std::string& candidate, const std::string& current) {
    const auto first = versionParts(candidate), second = versionParts(current);
    return first && second && *first > *second;
}
bool automaticCheckDue(std::uint64_t now, std::uint64_t previous) {
    return !previous || previous > now || now - previous >= 24 * 60 * 60;
}
UpdateResult parseReleaseResponse(unsigned status, const std::string& body, const std::string& current) {
    if (status == 404) return {UpdatePhase::NoRelease, Text::NoRelease, {}, {}};
    if (status == 403 || status == 429) return failure(Text::UpdateRateLimit);
    if (status != 200) return failure({Text::UpdateHttpError, {std::to_wstring(status)}});
    if (body.size() > 256 * 1024) return failure(Text::UpdateTooLarge);
    try {
        const auto json = nlohmann::json::parse(body);
        if (!json.is_object() || json.at("draft").get<bool>() || json.at("prerelease").get<bool>())
            return failure(Text::UpdateNotStable);
        const auto tag = json.at("tag_name").get<std::string>();
        if (!versionParts(tag) || !versionParts(current)) return failure(Text::UpdateInvalidVersion);
        const std::string version = tag.front() == 'v' ? tag.substr(1) : tag;
        const std::string page = std::string(ReleasesUrl) + "/tag/" + tag;
        const std::string name = "TS3-SoundBridge-" + version + "-win64.ts3_plugin";
        const std::string download = std::string(ReleasesUrl) + "/download/" + tag + "/" + name;
        bool found = false;
        if (!json.at("assets").is_array()) return failure(Text::UpdateInvalidAssets);
        for (const auto& asset : json.at("assets"))
            if (asset.at("name").get<std::string>() == name && asset.at("browser_download_url").get<std::string>() == download) found = true;
        if (!found) return failure(Text::UpdateNoInstaller);
        if (newerVersion(tag, current)) return {UpdatePhase::Available,
            {Text::UpdateAvailable, {std::wstring(version.begin(), version.end())}}, version, page};
        return {UpdatePhase::Current, {Text::UpdateCurrent, {std::wstring(current.begin(), current.end())}}, version, page};
    } catch (...) { return failure(Text::UpdateParseError); }
}
UpdateChecker::UpdateChecker(std::wstring file, bool automatic, Notification notification, Fetcher fetcher, std::chrono::milliseconds delay)
    : settingsFile_(std::move(file)), notification_(std::move(notification)), fetcher_(std::move(fetcher)), automatic_(automatic), startupDelay_(delay) {
    worker_ = std::thread(&UpdateChecker::run, this);
}
UpdateChecker::~UpdateChecker() {
    { std::lock_guard<std::mutex> lock(mutex_); closing_ = true; }
    wake_.notify_all(); if (worker_.joinable()) worker_.join();
}
void UpdateChecker::checkNow() {
    { std::lock_guard<std::mutex> lock(mutex_);
      if (closing_ || status_.phase == UpdatePhase::Checking) return;
      manual_ = true; }
    wake_.notify_all();
}
void UpdateChecker::setAutomatic(bool enabled) {
    { std::lock_guard<std::mutex> lock(mutex_); automatic_ = enabled; }
    wake_.notify_all();
}
UpdateResult UpdateChecker::status() const { std::lock_guard<std::mutex> lock(mutex_); return status_; }
UpdateResult UpdateChecker::fetch() {
    InternetHandle session{WinHttpOpen(L"TS3-SoundBridge/" SOUNDBRIDGE_VERSION_WSTRING,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return failure(Text::UpdateInitError);
    WinHttpSetTimeouts(session.value, 2000, 2000, 2000, 2000);
    InternetHandle connection{WinHttpConnect(session.value, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return failure(Text::UpdateConnectError);
    InternetHandle request{WinHttpOpenRequest(connection.value, L"GET", L"/repos/MaidTendouAris/ts3-soundbridge/releases/latest",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value) return failure(Text::UpdateRequestError);
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    const wchar_t headers[] = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2026-03-10\r\n";
    if (!WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr))
        return failure(Text::UpdateReadError);
    DWORD httpStatus = 0, length = sizeof(httpStatus);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
        &httpStatus, &length, WINHTTP_NO_HEADER_INDEX)) return failure(Text::UpdateStatusError);
    if (httpStatus != 200) return parseReleaseResponse(httpStatus, {}, SOUNDBRIDGE_VERSION_STRING);
    std::string body;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        { std::lock_guard<std::mutex> lock(mutex_); if (closing_) return failure(Text::UpdateCancelled); }
        char bytes[8192]; DWORD read = 0;
        if (!WinHttpReadData(request.value, bytes, sizeof(bytes), &read)) return failure(Text::UpdateInterrupted);
        if (!read) return parseReleaseResponse(httpStatus, body, SOUNDBRIDGE_VERSION_STRING);
        body.append(bytes, read);
        if (body.size() > 256 * 1024) return failure(Text::UpdateTooLarge);
    }
    return failure(Text::UpdateTimeout);
}
void UpdateChecker::run() noexcept {
    try {
        auto lastAutomatic = loadLastUpdateCheck(settingsFile_);
        const auto startup = std::chrono::steady_clock::now() + startupDelay_;
        std::string notified;
        std::unique_lock<std::mutex> lock(mutex_);
        while (!closing_) {
            const auto now = unixTime();
            const bool automatic = automatic_ && std::chrono::steady_clock::now() >= startup && automaticCheckDue(now, lastAutomatic);
            if (!manual_ && !automatic) { wake_.wait_for(lock, std::chrono::seconds(1)); continue; }
            manual_ = false;
            status_ = {UpdatePhase::Checking, Text::UpdateChecking, {}, {}};
            lock.unlock();
            lastAutomatic = now; saveLastUpdateCheck(settingsFile_, now);
            auto result = fetcher_ ? fetcher_() : fetch();
            lock.lock(); if (closing_) break;
            status_ = result;
            if (result.phase == UpdatePhase::Available && result.version != notified && notification_) {
                notified = result.version; lock.unlock(); notification_(result); lock.lock();
            }
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_); status_ = failure(Text::UpdateError);
    }
}
}

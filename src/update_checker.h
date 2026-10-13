#pragma once
#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace soundbridge {
inline constexpr char ReleasesUrl[] = "https://github.com/MaidTendouAris/ts3-soundbridge/releases";
inline constexpr wchar_t ReleasesUrlWide[] = L"https://github.com/MaidTendouAris/ts3-soundbridge/releases";
enum class UpdatePhase { Idle, Checking, Current, Available, NoRelease, Error };
struct UpdateResult {
    UpdatePhase phase = UpdatePhase::Idle;
    std::wstring message = L"更新来自 GitHub Releases。";
    std::string version, pageUrl;
};
bool newerVersion(const std::string& candidate, const std::string& current);
UpdateResult parseReleaseResponse(unsigned status, const std::string& body, const std::string& current);
bool automaticCheckDue(std::uint64_t now, std::uint64_t previous);
class UpdateChecker {
public:
    using Fetcher = std::function<UpdateResult()>;
    using Notification = std::function<void(const UpdateResult&)>;
    UpdateChecker(std::wstring settingsFile, bool automatic, Notification notification = {}, Fetcher fetcher = {}, std::chrono::milliseconds startupDelay = std::chrono::seconds(15));
    ~UpdateChecker();
    void checkNow();
    void setAutomatic(bool enabled);
    UpdateResult status() const;
private:
    void run() noexcept;
    UpdateResult fetch();
    std::wstring settingsFile_;
    Notification notification_;
    Fetcher fetcher_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    UpdateResult status_;
    bool closing_ = false, automatic_ = true, manual_ = false;
    std::thread worker_;
    std::chrono::milliseconds startupDelay_;
};
}

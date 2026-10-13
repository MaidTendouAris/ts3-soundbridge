#include "update_checker.h"
#include "settings.h"
#include "soundbridge_version.h"
#include <filesystem>
#include <iostream>

using namespace soundbridge;
namespace {
int failures = 0;
void check(bool value, const char* label) { if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
std::string release(const std::string& version, const std::string& flags = "\"draft\":false,\"prerelease\":false") {
    return "{" + flags + ",\"tag_name\":\"v" + version + "\",\"assets\":[{\"name\":\"TS3-SoundBridge-" + version +
        "-win64.ts3_plugin\",\"browser_download_url\":\"" + ReleasesUrl + "/download/v" + version + "/TS3-SoundBridge-" + version + "-win64.ts3_plugin\"}]}";
}
template<typename Predicate> bool wait(Predicate predicate) {
    for (int i = 0; i < 300; ++i) { if (predicate()) return true; Sleep(10); } return false;
}
}
int main(int argc, char** argv) {
    const auto file = (std::filesystem::current_path() / "update-test-settings.ini").wstring();
    DeleteFileW(file.c_str());
    if (argc > 1 && std::string(argv[1]) == "--live") {
        UpdateChecker checker(file, false); checker.checkNow();
        for (int i = 0; i < 300 && (checker.status().phase == UpdatePhase::Idle || checker.status().phase == UpdatePhase::Checking); ++i) Sleep(100);
        const auto result = checker.status();
        std::cout << "Live phase=" << static_cast<int>(result.phase) << " version=" << result.version << " page=" << result.pageUrl << '\n';
        DeleteFileW(file.c_str());
        return result.phase == UpdatePhase::Current || result.phase == UpdatePhase::Available || result.phase == UpdatePhase::NoRelease ? 0 : 1;
    }
    check(newerVersion("v1.10.0", "1.9.9") && newerVersion("2.0.0", "1.100.100"), "numeric semantic versions");
    check(!newerVersion("v1.0.0", "1.0.0") && !newerVersion("0.9.9", "1.0.0"), "equal and older releases");
    for (const auto* version : {"1.0", "1.0.0-beta", "1.0.0+dev", "01.0.0", "999999999999.0.0", "1.0.0/evil", "-1.0.0"})
        check(!newerVersion(version, "1.0.0"), "reject invalid or non-stable versions");
    check(parseReleaseResponse(404, "{}", "1.0.0").phase == UpdatePhase::NoRelease, "no release is distinct from current");
    check(parseReleaseResponse(403, "{}", "1.0.0").phase == UpdatePhase::Error, "rate limit is not current");
    check(parseReleaseResponse(500, "{}", "1.0.0").phase == UpdatePhase::Error, "HTTP failure is not current");
    check(parseReleaseResponse(200, release("1.0.0"), "1.0.0").phase == UpdatePhase::Current, "current release");
    const auto available = parseReleaseResponse(200, release("1.1.0"), "1.0.0");
    check(available.phase == UpdatePhase::Available && available.version == "1.1.0" && available.pageUrl == std::string(ReleasesUrl) + "/tag/v1.1.0", "new release and canonical repository URL");
    check(parseReleaseResponse(200, release("1.1.0", "\"draft\":false,\"prerelease\":true"), "1.0.0").phase == UpdatePhase::Error, "exclude prereleases");
    for (const auto* body : {"{broken}", "[]", "{}", "{\"draft\":\"no\"}", "{\"draft\":false,\"prerelease\":false,\"tag_name\":\"v1.1.0\",\"assets\":[]}"})
        check(parseReleaseResponse(200, body, "1.0.0").phase == UpdatePhase::Error, "malformed or incomplete response rejected");
    auto untrusted = release("1.1.0"); untrusted.replace(untrusted.find("https://github.com"), 18, "https://evil.test");
    check(parseReleaseResponse(200, untrusted, "1.0.0").phase == UpdatePhase::Error, "untrusted download URL rejected");
    check(parseReleaseResponse(200, std::string(300 * 1024, 'x'), "1.0.0").phase == UpdatePhase::Error, "response size limit");
    check(automaticCheckDue(100000, 0) && automaticCheckDue(100000, 13600) && !automaticCheckDue(100000, 99999), "24 hour automatic cadence");
    check(automaticCheckDue(100000, 100001), "clock change recovery");
    std::atomic<int> calls{0}, notifications{0};
    {
        UpdateChecker checker(file, false, [&](const auto&) { ++notifications; }, [&] { ++calls; Sleep(100); return available; }, std::chrono::milliseconds(0));
        Sleep(50); check(calls == 0, "automatic preference disabled");
        checker.checkNow(); check(wait([&] { return checker.status().phase == UpdatePhase::Checking; }), "manual request starts in background");
        checker.checkNow(); check(wait([&] { return checker.status().phase == UpdatePhase::Available; }), "manual result returned");
        check(calls == 1 && wait([&] { return notifications == 1; }), "one network request at a time and new version notified");
        checker.checkNow(); check(wait([&] { return calls == 2 && checker.status().phase == UpdatePhase::Available; }), "manual recheck allowed");
        check(notifications == 1, "same available version notified once per session");
        check(loadLastUpdateCheck(file) > 0, "check time persisted");
    }
    DeleteFileW(file.c_str()); calls = 0;
    {
        UpdateChecker checker(file, true, {}, [&] { ++calls; return parseReleaseResponse(200, release("1.0.0"), "1.0.0"); }, std::chrono::milliseconds(0));
        check(wait([&] { return checker.status().phase == UpdatePhase::Current; }) && calls == 1, "automatic startup check");
        checker.setAutomatic(false); Sleep(30); check(calls == 1, "automatic cadence does not loop");
    }
    DeleteFileW(file.c_str());
    if (failures) return 1;
    std::cout << "All update parsing, scheduler, preference and notification tests passed.\n";
}

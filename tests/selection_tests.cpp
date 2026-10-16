#include "bridge_engine.h"
#include <filesystem>
#include <iostream>

using namespace soundbridge;
namespace {
int failures = 0;
void check(bool value, const char* message) { if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
ProcessInfo process(DWORD pid, DWORD parent, std::wstring path, std::uint64_t created = 1) {
    return {pid, parent, created, path, path.substr(path.find_last_of(L"\\") + 1)};
}
}
int main() {
    std::vector<ProcessInfo> processes{
        process(1, 0, L"C:\\launcher.exe"), process(10, 1, L"C:\\ts3.exe", 2), process(11, 10, L"C:\\ts-helper.exe", 3),
        process(20, 0, L"C:\\Player.exe"), process(21, 20, L"C:\\Player.exe", 2),
        process(22, 21, L"C:\\renderer.exe", 3), process(30, 0, L"C:\\Other.exe"), process(31, 0, L"C:\\Other.exe")};
    std::wstring error;
    auto roots = selectRoots(processes, {L"c:\\PLAYER.exe", L"C:\\renderer.exe", L"C:\\Other.exe"}, 10, error);
    check(error.empty() && roots.size() == 3 && roots[0].pid == 20 && roots[1].pid == 30 && roots[2].pid == 31, "deduplicate child trees, include independent instances and paths case-insensitively");
    for (const auto& path : {L"C:\\ts3.exe", L"C:\\ts-helper.exe", L"C:\\launcher.exe"}) {
        roots = selectRoots(processes, {path}, 10, error);
        check(roots.empty() && !error.empty(), "reject TS3, its child and its ancestor");
    }
    roots = selectRoots(processes, {L"C:\\not-running.exe"}, 10, error);
    check(roots.empty() && error.empty(), "missing selected application never falls back to all audio");
    processes.clear();
    for (DWORD i = 100; i < 117; ++i) processes.push_back(process(i, 0, L"C:\\Many.exe"));
    roots = selectRoots(processes, {L"C:\\Many.exe"}, 10, error);
    check(roots.empty() && !error.empty(), "16 independent process-tree limit");
    const auto file = (std::filesystem::current_path() / L"selection-test-settings.ini").wstring();
    DeleteFileW(file.c_str());
    Settings settings; settings.mode = CaptureMode::SelectedApplications; settings.desktopPercent = 73;
    check(!loadSettings(file).showLevelMeters, "missing display preference defaults to meters off");
    settings.showLevelMeters = true;
    settings.microphonePercent = 42; settings.microphoneMuted = true;
    settings.applications = {L"D:\\音乐 播放器\\播放器.exe", L"C:\\Second.exe"};
    check(saveSettings(file, settings), "save Unicode settings");
    const auto loaded = loadSettings(file);
    check(loaded.mode == settings.mode && loaded.desktopPercent == 73 && loaded.microphonePercent == 42 &&
        loaded.microphoneMuted && loaded.showLevelMeters && loaded.applications == settings.applications, "Unicode configuration and display preference round trip");
    settings.desktopPercent = 300; settings.microphonePercent = 275;
    check(saveSettings(file, settings), "save expanded gains");
    check(loadSettings(file).desktopPercent == 300 && loadSettings(file).microphonePercent == 275, "expanded gain range survives persistence");
    WritePrivateProfileStringW(L"SoundBridge", L"DesktopPercent", L"301", file.c_str());
    WritePrivateProfileStringW(L"SoundBridge", L"MicrophonePercent", L"-1", file.c_str());
    check(loadSettings(file).desktopPercent == 50 && loadSettings(file).microphonePercent == 100, "invalid gain uses safe defaults");
    DeleteFileW(file.c_str());
    BridgeEngine engine(settings, [](std::uint64_t, std::wstring& problem) { problem = L"disconnected"; return false; });
    check(engine.settings().desktopPercent == 300 && engine.settings().microphonePercent == 275, "engine retains expanded gains");
    auto outOfRange = settings; outOfRange.desktopPercent = 999; outOfRange.microphonePercent = -1; engine.apply(outOfRange);
    check(engine.settings().desktopPercent == 300 && engine.settings().microphonePercent == 0, "engine bounds expanded gains");
    check(engine.requestedServer() == 0, "loading settings never starts sharing");
    short sample = 1234; int flags = 2; engine.edit(42, &sample, 1, 1, &flags);
    check(sample == 1234 && flags == 2, "inactive engine preserves voice");
    engine.start(42);
    for (int i = 0; i < 100 && engine.status().phase != BridgePhase::Error; ++i) Sleep(10);
    check(engine.status().phase == BridgePhase::Error && engine.requestedServer() == 0, "validator failure stops request");
    engine.stop(); check(engine.status().phase == BridgePhase::Stopped, "explicit stop clears error");
    if (failures) return 1;
    std::cout << "All application selection, persistence and controller tests passed.\n";
}

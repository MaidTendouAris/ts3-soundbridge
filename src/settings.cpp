#include "settings.h"
#include <Windows.h>
#include <algorithm>
#include <mutex>

namespace soundbridge {
namespace { std::mutex profileMutex; }
Settings loadSettings(const std::wstring& file) {
    std::lock_guard<std::mutex> lock(profileMutex);
    Settings settings;
    if (file.empty()) return settings;
    wchar_t language[85]{};
    GetPrivateProfileStringW(L"SoundBridge", L"Language", L"", language, 85, file.c_str());
    settings.language = languageFromCode(language, settings.language);
    settings.mode = GetPrivateProfileIntW(L"SoundBridge", L"Mode", 0, file.c_str()) == 1 ?
        CaptureMode::SelectedApplications : CaptureMode::AllOtherAudio;
    const auto readGain = [&](const wchar_t* key, int fallback) {
        const UINT value = GetPrivateProfileIntW(L"SoundBridge", key, fallback, file.c_str());
        return value <= MaximumGainPercent ? static_cast<int>(value) : fallback;
    };
    settings.desktopPercent = readGain(L"DesktopPercent", 50);
    settings.microphonePercent = readGain(L"MicrophonePercent", 100);
    settings.microphoneMuted = GetPrivateProfileIntW(L"SoundBridge", L"MicrophoneMuted", 0, file.c_str()) == 1;
    settings.showLevelMeters = GetPrivateProfileIntW(L"SoundBridge", L"ShowLevelMeters", 0, file.c_str()) == 1;
    settings.autoCheckUpdates = GetPrivateProfileIntW(L"SoundBridge", L"AutoCheckUpdates", 1, file.c_str()) != 0;
    const UINT count = std::min(GetPrivateProfileIntW(L"Applications", L"Count", 0, file.c_str()), 64u);
    std::vector<wchar_t> path(32768);
    for (UINT i = 0; i < count; ++i) {
        const auto key = L"App" + std::to_wstring(i);
        GetPrivateProfileStringW(L"Applications", key.c_str(), L"", path.data(), static_cast<DWORD>(path.size()), file.c_str());
        if (path[0]) settings.applications.emplace_back(path.data());
    }
    return settings;
}
bool saveSettings(const std::wstring& file, const Settings& settings) {
    std::lock_guard<std::mutex> lock(profileMutex);
    if (file.empty()) return false;
    // A fresh Unicode INI uses UTF-16; this preserves non-ASCII application paths.
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        const WORD bom = 0xFEFF; DWORD written = 0;
        const bool initialized = WriteFile(handle, &bom, sizeof(bom), &written, nullptr) && written == sizeof(bom);
        CloseHandle(handle);
        if (!initialized) return false;
    }
    bool success = true;
    const auto write = [&](const wchar_t* section, const wchar_t* key, const std::wstring& value) {
        if (!WritePrivateProfileStringW(section, key, value.c_str(), file.c_str())) success = false;
    };
    write(L"SoundBridge", L"Language", languageCode(settings.language));
    write(L"SoundBridge", L"Mode", settings.mode == CaptureMode::SelectedApplications ? L"1" : L"0");
    write(L"SoundBridge", L"DesktopPercent", std::to_wstring(std::clamp(settings.desktopPercent, 0, MaximumGainPercent)));
    write(L"SoundBridge", L"MicrophonePercent", std::to_wstring(std::clamp(settings.microphonePercent, 0, MaximumGainPercent)));
    write(L"SoundBridge", L"MicrophoneMuted", settings.microphoneMuted ? L"1" : L"0");
    write(L"SoundBridge", L"ShowLevelMeters", settings.showLevelMeters ? L"1" : L"0");
    write(L"SoundBridge", L"AutoCheckUpdates", settings.autoCheckUpdates ? L"1" : L"0");
    if (!WritePrivateProfileStringW(L"Applications", nullptr, nullptr, file.c_str())) success = false;
    write(L"Applications", L"Count", std::to_wstring(std::min<std::size_t>(settings.applications.size(), 64)));
    for (std::size_t i = 0; i < settings.applications.size() && i < 64; ++i) {
        const auto key = L"App" + std::to_wstring(i);
        write(L"Applications", key.c_str(), settings.applications[i]);
    }
    // The documented return value is zero when flushing the profile cache;
    // the individual writes above determine whether saving succeeded.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
    return success;
}
std::uint64_t loadLastUpdateCheck(const std::wstring& file) {
    std::lock_guard<std::mutex> lock(profileMutex);
    wchar_t value[32]{};
    GetPrivateProfileStringW(L"Updates", L"LastCheck", L"0", value, 32, file.c_str());
    wchar_t* end = nullptr;
    const auto result = _wcstoui64(value, &end, 10);
    return end && !*end && value[0] != L'-' ? result : 0;
}
bool saveLastUpdateCheck(const std::wstring& file, std::uint64_t timestamp) {
    // Ensure a new configuration has its UTF-16 BOM before writing the timestamp.
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES && !saveSettings(file, loadSettings(file))) return false;
    std::lock_guard<std::mutex> lock(profileMutex);
    return WritePrivateProfileStringW(L"Updates", L"LastCheck", std::to_wstring(timestamp).c_str(), file.c_str()) != FALSE;
}
}

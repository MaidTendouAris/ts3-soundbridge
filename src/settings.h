#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace soundbridge {
inline constexpr int MaximumGainPercent = 300;
enum class CaptureMode { AllOtherAudio, SelectedApplications };
struct Settings {
    CaptureMode mode = CaptureMode::AllOtherAudio;
    int desktopPercent = 50;
    int microphonePercent = 100;
    bool microphoneMuted = false;
    bool autoCheckUpdates = true;
    std::vector<std::wstring> applications;
};
Settings loadSettings(const std::wstring& file);
bool saveSettings(const std::wstring& file, const Settings& settings);
std::uint64_t loadLastUpdateCheck(const std::wstring& file);
bool saveLastUpdateCheck(const std::wstring& file, std::uint64_t timestamp);
}

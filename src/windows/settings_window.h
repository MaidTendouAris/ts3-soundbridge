#pragma once
#include "bridge_engine.h"
#include "update_checker.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace soundbridge {
class SettingsWindow {
public:
    SettingsWindow(BridgeEngine& engine, UpdateChecker& updates, std::wstring settingsFile, std::function<std::uint64_t()> currentServer);
    ~SettingsWindow();
    void show();
    void close();
private:
    static INT_PTR CALLBACK procedure(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
    INT_PTR handle(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
    void run();
    void fill(const Settings& settings);
    Settings draft() const;
    bool apply();
    void refreshApplications(const std::vector<std::wstring>& selected);
    void updateMode();
    void updateLabels();
    void updateStatus();
    void updateUpdateStatus();
    void fitToScreen();
    BridgeEngine& engine_;
    UpdateChecker& updates_;
    std::wstring settingsFile_, notice_;
    std::function<std::uint64_t()> currentServer_;
    std::vector<AudioApplication> applications_;
    std::atomic<HWND> window_{nullptr};
    std::atomic<bool> closing_{false}, showRequested_{false};
    std::mutex lifecycle_;
    std::thread thread_;
    HFONT font_ = nullptr, headerFont_ = nullptr;
    ULONGLONG noticeUntil_ = 0;
};
}

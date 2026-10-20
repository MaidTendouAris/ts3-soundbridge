#include "settings_window.h"
#include "gui_resource.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <shellapi.h>
#include "soundbridge_version.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace soundbridge {
namespace {
void windowModuleAddress() {}
void setControlText(HWND window, int id, const wchar_t* text) {
    wchar_t previous[256]{}; GetDlgItemTextW(window, id, previous, 256);
    if (std::wstring(previous) != text) SetDlgItemTextW(window, id, text);
}
struct ThemeScope {
    HANDLE context = INVALID_HANDLE_VALUE;
    ULONG_PTR cookie = 0;
    explicit ThemeScope(HMODULE module) {
        ACTCTXW settings{}; settings.cbSize = sizeof(settings);
        settings.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
        settings.hModule = module; settings.lpResourceName = MAKEINTRESOURCEW(3);
        context = CreateActCtxW(&settings);
        if (context != INVALID_HANDLE_VALUE) ActivateActCtx(context, &cookie);
    }
    ~ThemeScope() {
        if (cookie) DeactivateActCtx(0, cookie);
        if (context != INVALID_HANDLE_VALUE) ReleaseActCtx(context);
    }
};
}
SettingsWindow::SettingsWindow(BridgeEngine& engine, UpdateChecker& updates, std::wstring file, std::function<std::uint64_t()> server)
    : engine_(engine), updates_(updates), settingsFile_(std::move(file)), currentServer_(std::move(server)) {}
SettingsWindow::~SettingsWindow() { close(); }
void SettingsWindow::show() {
    std::lock_guard<std::mutex> lock(lifecycle_);
    if (closing_.load()) return;
    showRequested_.store(true);
    if (!thread_.joinable()) thread_ = std::thread(&SettingsWindow::run, this);
    else if (auto window = window_.load()) PostMessageW(window, WM_SB_SHOW, 0, 0);
}
void SettingsWindow::close() {
    std::lock_guard<std::mutex> lock(lifecycle_);
    closing_.store(true);
    if (auto window = window_.load()) PostMessageW(window, WM_SB_SHUTDOWN, 0, 0);
    if (thread_.joinable()) thread_.join();
}
void SettingsWindow::run() {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&windowModuleAddress), &module);
    ThemeScope theme(module);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    const auto dialog = CreateDialogParamW(module, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, procedure, reinterpret_cast<LPARAM>(this));
    if (!dialog) return;
    window_.store(dialog);
    if (closing_.load()) DestroyWindow(dialog);
    else if (showRequested_.exchange(false)) { ShowWindow(dialog, SW_SHOW); refreshMetering(); SetForegroundWindow(dialog); }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    window_.store(nullptr);
    if (headerFont_) { DeleteObject(headerFont_); headerFont_ = nullptr; }
    if (font_) { DeleteObject(font_); font_ = nullptr; }
}
INT_PTR CALLBACK SettingsWindow::procedure(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        self = reinterpret_cast<SettingsWindow*>(lparam);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_.store(dialog);
    }
    if (!self) return FALSE;
    try { return self->handle(dialog, message, wparam, lparam); }
    catch (...) { SetDlgItemTextW(dialog, IDC_STATUS, tr(Text::SettingsError)); return TRUE; }
}
INT_PTR SettingsWindow::handle(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_INITDIALOG: {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&windowModuleAddress), &module);
        const auto dpi = GetDpiForWindow(dialog);
        for (const auto type : {ICON_SMALL, ICON_BIG}) {
            const int metric = type == ICON_SMALL ? SM_CXSMICON : SM_CXICON;
            const auto icon = LoadImageW(module, MAKEINTRESOURCEW(IDI_SOUNDBRIDGE), IMAGE_ICON, GetSystemMetricsForDpi(metric, dpi), GetSystemMetricsForDpi(metric, dpi), LR_SHARED);
            SendMessageW(dialog, WM_SETICON, type, reinterpret_cast<LPARAM>(icon));
        }
        const auto list = GetDlgItem(dialog, IDC_APPS);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        SetWindowTheme(list, L"Explorer", nullptr);
        LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH;
        RECT client{}; GetClientRect(list, &client);
        const wchar_t* labels[] = { tr(Text::AppColumn), tr(Text::StateColumn), tr(Text::PathColumn) };
        const int percentages[] = { 37, 17, 46 };
        for (int i = 0; i < 3; ++i) {
            column.pszText = const_cast<wchar_t*>(labels[i]); column.cx = (client.right - 20) * percentages[i] / 100;
            ListView_InsertColumn(list, i, &column);
        }
        for (int id : {IDC_DESKTOP_SLIDER, IDC_MIC_SLIDER}) {
            SendDlgItemMessageW(dialog, id, TBM_SETRANGE, TRUE, MAKELPARAM(0, MaximumGainPercent));
            SendDlgItemMessageW(dialog, id, TBM_SETPAGESIZE, 0, 10);
        }
        for (int id : {IDC_DESKTOP_METER, IDC_MIC_METER}) {
            const auto meter = GetDlgItem(dialog, id);
            // Disable themed animation: each position represents the latest audio
            // interval, rather than an animated progress toward a target value.
            SetWindowTheme(meter, L"", L"");
            SendMessageW(meter, PBM_SETRANGE32, 0, 600);
            SendMessageW(meter, PBM_SETBKCOLOR, 0, RGB(230, 232, 238));
        }
        for (int i = 0; i < static_cast<int>(Language::Count); ++i)
            SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(languageName(static_cast<Language>(i))));
        fitToScreen(); fill(engine_.settings());
        SetTimer(dialog, 1, 500, nullptr);
        return TRUE;
    }
    case WM_SB_SHOW:
        showRequested_.store(false);
        if (!pendingSettings_) fill(engine_.settings()); else updateStatus();
        ShowWindow(dialog, SW_RESTORE); refreshMetering(); SetForegroundWindow(dialog); return TRUE;
    case WM_SB_SHUTDOWN: DestroyWindow(dialog); return TRUE;
    case WM_CLOSE: saveLiveGains(); stopMetering(); ShowWindow(dialog, SW_HIDE); return TRUE;
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) stopMetering(); else refreshMetering();
        return FALSE;
    case WM_DESTROY: saveLiveGains(); stopMetering(); KillTimer(dialog, 1); KillTimer(dialog, 3); PostQuitMessage(0); return TRUE;
    case WM_TIMER:
        if (wparam == 2 && meterTimerRunning_) updateLevels();
        else if (wparam == 1) { updateStatus(); updateUpdateStatus(); }
        else if (wparam == 3) saveLiveGains();
        return TRUE;
    case WM_HSCROLL:
        updateLabels(); applyLiveGains(LOWORD(wparam) == TB_ENDTRACK); return TRUE;
    case WM_NOTIFY:
        if (lparam) {
            const auto* notification = reinterpret_cast<NMHDR*>(lparam);
            if (notification->idFrom == IDC_APPS && notification->code == LVN_ITEMCHANGED) {
                const auto* change = reinterpret_cast<NMLISTVIEW*>(lparam);
                if ((change->uChanged & LVIF_STATE) && ((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK)) updatePendingState();
            }
        }
        return FALSE;
    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lparam) == GetDlgItem(dialog, IDC_SETTINGS_HINT)) {
            const auto dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, pendingSettings_ || gainsSaveFailed_ ? RGB(156, 83, 0) : gainsSavePending_ ? RGB(33, 91, 164) : GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_LANGUAGE:
            if (HIWORD(wparam) == CBN_SELCHANGE && !updatingControls_) changeLanguage();
            return TRUE;
        case IDCANCEL: saveLiveGains(); stopMetering(); ShowWindow(dialog, SW_HIDE); return TRUE;
        case IDC_SHOW_METERS: toggleMeters(); return TRUE;
        case IDC_MODE_ALL: case IDC_MODE_SELECTED: updateMode(); updatePendingState(); return TRUE;
        case IDC_MIC_MUTE: case IDC_AUTO_UPDATE: updatePendingState(); return TRUE;
        case IDC_REFRESH: refreshApplications(draft().applications); return TRUE;
        case IDC_CLEAR: {
            updatingControls_ = true;
            const auto list = GetDlgItem(dialog, IDC_APPS);
            for (int i = 0; i < ListView_GetItemCount(list); ++i) ListView_SetCheckState(list, i, FALSE);
            updatingControls_ = false; updatePendingState();
            return TRUE;
        }
        case IDC_DETAILS: updateStatus(); return TRUE;
        case IDC_CHECK_UPDATE: updates_.checkNow(); updateUpdateStatus(); return TRUE;
        case IDC_DOWNLOAD_UPDATE: {
            const auto result = updates_.status();
            const auto page = result.pageUrl.empty() ? std::wstring(ReleasesUrlWide) : std::wstring(result.pageUrl.begin(), result.pageUrl.end());
            ShellExecuteW(dialog, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return TRUE;
        }
        case IDC_APPLY: apply(); return TRUE;
        case IDC_START:
            if (apply()) { notice_.clear(); engine_.start(currentServer_()); updateStatus(); }
            return TRUE;
        case IDC_STOP: notice_.clear(); engine_.stop(); updateStatus(); return TRUE;
        }
        break;
    }
    return FALSE;
}
void SettingsWindow::fill(const Settings& settings) {
    const auto window = window_.load();
    updatingControls_ = true; appliedSettings_ = settings;
    stopMetering(); engine_.takeLevels();
    notice_.clear(); noticeUntil_ = 0;
    CheckRadioButton(window, IDC_MODE_ALL, IDC_MODE_SELECTED, settings.mode == CaptureMode::SelectedApplications ? IDC_MODE_SELECTED : IDC_MODE_ALL);
    SendDlgItemMessageW(window, IDC_DESKTOP_SLIDER, TBM_SETPOS, TRUE, settings.desktopPercent);
    SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_SETPOS, TRUE, settings.microphonePercent);
    CheckDlgButton(window, IDC_MIC_MUTE, settings.microphoneMuted ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_SHOW_METERS, settings.showLevelMeters ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_AUTO_UPDATE, settings.autoCheckUpdates ? BST_CHECKED : BST_UNCHECKED);
    refreshApplications(settings.applications); localize(); updateLabels(); updateMode();
    updatingControls_ = false; updateStatus(); updateUpdateStatus();
}
void SettingsWindow::localize() {
    const auto window = window_.load();
    const bool wasUpdating = updatingControls_; updatingControls_ = true;
    const auto language = engine_.settings().language;
    setLanguage(language);
    SetWindowTextW(window, tr(Text::WindowTitle));
    const std::pair<int, Text> controls[] = {
        {IDC_HEADER, Text::Header}, {IDC_LANGUAGE_LABEL, Text::LanguageLabel}, {IDC_MODE_ALL, Text::ModeAll},
        {IDC_MODE_SELECTED, Text::ModeSelected}, {IDC_APP_HINT, Text::AppHint}, {IDC_CLEAR, Text::Clear},
        {IDC_REFRESH, Text::Refresh}, {IDC_VOLUME_GROUP, Text::VolumeGroup}, {IDC_SHOW_METERS, Text::ShowMeters},
        {IDC_SHARED_LABEL, Text::SharedAudio}, {IDC_MIC_LABEL, Text::Microphone}, {IDC_STATUS_LABEL, Text::StatusLabel},
        {IDC_DETAILS, Text::Details}, {IDC_CHECK_UPDATE, Text::CheckUpdate}, {IDC_DOWNLOAD_UPDATE, Text::Releases},
        {IDC_START, Text::Start}, {IDC_STOP, Text::Stop}, {IDCANCEL, Text::Close}
    };
    for (const auto& control : controls) SetDlgItemTextW(window, control.first, tr(control.second));
    SetDlgItemTextW(window, IDC_UPDATE_GROUP, format(Text::UpdateGroup, {SOUNDBRIDGE_VERSION_WSTRING}).c_str());
    SendDlgItemMessageW(window, IDC_LANGUAGE, CB_SETCURSEL, static_cast<int>(language), 0);
    const auto list = GetDlgItem(window, IDC_APPS);
    const Text columns[] = {Text::AppColumn, Text::StateColumn, Text::PathColumn};
    LVCOLUMNW column{}; column.mask = LVCF_TEXT;
    for (int i = 0; i < 3; ++i) {
        column.pszText = const_cast<wchar_t*>(tr(columns[i])); ListView_SetColumn(list, i, &column);
    }
    for (int i = 0; i < static_cast<int>(applications_.size()); ++i) {
        const auto& app = applications_[i];
        const auto state = app.pids.empty() ? Text::NotDetected : app.playing ? Text::Playing : Text::Paused;
        ListView_SetItemText(list, i, 1, const_cast<wchar_t*>(tr(state)));
    }
    LOGFONTW font{};
    const auto previousFont = font_, previousHeader = headerFont_;
    GetObjectW(previousFont ? previousFont : reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)), sizeof(font), &font);
    const wchar_t* faces[] = {L"Segoe UI", L"Microsoft YaHei UI", L"Microsoft JhengHei UI", L"Malgun Gothic", L"Yu Gothic UI"};
    wcscpy_s(font.lfFaceName, faces[static_cast<int>(language)]);
    font.lfWeight = FW_NORMAL; font_ = CreateFontIndirectW(&font);
    font.lfWeight = FW_SEMIBOLD; font.lfHeight = static_cast<LONG>(font.lfHeight * 1.2);
    headerFont_ = CreateFontIndirectW(&font);
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SendDlgItemMessageW(window, IDC_HEADER, WM_SETFONT, reinterpret_cast<WPARAM>(headerFont_), TRUE);
    if (headerWarning_) SendDlgItemMessageW(window, IDC_SETTINGS_HINT, WM_SETFONT, reinterpret_cast<WPARAM>(headerFont_), TRUE);
    if (previousFont) DeleteObject(previousFont);
    if (previousHeader) DeleteObject(previousHeader);
    updatingControls_ = wasUpdating;
    updateStatus(); updateUpdateStatus();
    InvalidateRect(window, nullptr, TRUE);
}
void SettingsWindow::changeLanguage() {
    const auto window = window_.load();
    const auto index = SendDlgItemMessageW(window, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
    if (index < 0 || index >= static_cast<int>(Language::Count)) return;
    auto settings = engine_.settings();
    const auto previous = settings.language;
    settings.language = static_cast<Language>(index);
    if (settings.language == previous) return;
    // Save only applied state plus the language; leave other controls as drafts.
    if (!saveSettings(settingsFile_, settings)) {
        SendDlgItemMessageW(window, IDC_LANGUAGE, CB_SETCURSEL, static_cast<int>(previous), 0);
        notice_ = Text::SettingsSaveError; noticeUntil_ = 0; updateStatus(); return;
    }
    engine_.apply(std::move(settings));
    KillTimer(window, 3); gainsSavePending_ = gainsSaveFailed_ = false;
    if (engine_.status().phase != BridgePhase::Error &&
        (notice_.empty() || notice_.key == Text::LanguageSaved || notice_.key == Text::SettingsApplied)) {
        notice_ = Text::LanguageSaved; noticeUntil_ = GetTickCount64() + 6000;
    }
    else if (notice_.key == Text::GainSaveError) { notice_.clear(); noticeUntil_ = 0; }
    localize();
}
Settings SettingsWindow::draft() const {
    const auto window = window_.load();
    Settings settings; settings.language = engine_.settings().language;
    settings.mode = IsDlgButtonChecked(window, IDC_MODE_SELECTED) == BST_CHECKED ? CaptureMode::SelectedApplications : CaptureMode::AllOtherAudio;
    settings.desktopPercent = static_cast<int>(SendDlgItemMessageW(window, IDC_DESKTOP_SLIDER, TBM_GETPOS, 0, 0));
    settings.microphonePercent = static_cast<int>(SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_GETPOS, 0, 0));
    settings.microphoneMuted = IsDlgButtonChecked(window, IDC_MIC_MUTE) == BST_CHECKED;
    settings.showLevelMeters = IsDlgButtonChecked(window, IDC_SHOW_METERS) == BST_CHECKED;
    settings.autoCheckUpdates = IsDlgButtonChecked(window, IDC_AUTO_UPDATE) == BST_CHECKED;
    const auto list = GetDlgItem(window, IDC_APPS);
    for (int i = 0; i < ListView_GetItemCount(list) && i < static_cast<int>(applications_.size()); ++i)
        if (ListView_GetCheckState(list, i)) settings.applications.push_back(applications_[i].path);
    return settings;
}
bool SettingsWindow::apply() {
    const auto settings = draft();
    if (settings.applications.size() > 64) {
        notice_ = Text::TooManyApps; noticeUntil_ = 0; updateStatus(); return false;
    }
    if (settings.mode == CaptureMode::SelectedApplications && settings.applications.empty()) {
        notice_ = Text::SelectAppApply; noticeUntil_ = 0; updateStatus(); return false;
    }
    if (!saveSettings(settingsFile_, settings)) {
        notice_ = Text::SettingsSaveError; noticeUntil_ = 0; updateStatus(); return false;
    }
    engine_.apply(settings); updates_.setAutomatic(settings.autoCheckUpdates);
    KillTimer(window_.load(), 3); gainsSavePending_ = gainsSaveFailed_ = false;
    appliedSettings_ = settings;
    notice_ = Text::SettingsApplied; noticeUntil_ = GetTickCount64() + 2000; updateStatus(); return true;
}
void SettingsWindow::refreshApplications(const std::vector<std::wstring>& selected) {
    const bool wasUpdating = updatingControls_; updatingControls_ = true;
    auto applications = audioApplications(GetCurrentProcessId());
    // Preserve configured choices even when the application is not running or
    // has not yet created an audio session. Binding follows the executable path.
    for (const auto& path : selected) {
        if (std::none_of(applications.begin(), applications.end(), [&](const auto& app) { return samePath(path, app.path); }))
            applications.push_back({path, path.substr(path.find_last_of(L"\\/") + 1), {}, false});
    }
    applications_ = std::move(applications);
    const auto list = GetDlgItem(window_.load(), IDC_APPS);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0); ListView_DeleteAllItems(list);
    for (std::size_t i = 0; i < applications_.size(); ++i) {
        auto& app = applications_[i];
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = app.name.data();
        const int row = ListView_InsertItem(list, &item);
        const wchar_t* state = app.pids.empty() ? tr(Text::NotDetected) : app.playing ? tr(Text::Playing) : tr(Text::Paused);
        ListView_SetItemText(list, row, 1, const_cast<wchar_t*>(state));
        ListView_SetItemText(list, row, 2, app.path.data());
        ListView_SetCheckState(list, row, std::any_of(selected.begin(), selected.end(), [&](const auto& path) { return samePath(path, app.path); }));
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0); InvalidateRect(list, nullptr, TRUE);
    updatingControls_ = wasUpdating; updatePendingState();
}
void SettingsWindow::updateMode() {
    const auto window = window_.load();
    const bool selected = IsDlgButtonChecked(window, IDC_MODE_SELECTED) == BST_CHECKED;
    EnableWindow(GetDlgItem(window, IDC_APPS), selected);
    EnableWindow(GetDlgItem(window, IDC_CLEAR), selected);
}
void SettingsWindow::updateLabels() {
    const auto window = window_.load();
    const auto desktop = std::to_wstring(SendDlgItemMessageW(window, IDC_DESKTOP_SLIDER, TBM_GETPOS, 0, 0)) + L"%";
    const auto microphone = std::to_wstring(SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_GETPOS, 0, 0)) + L"%";
    SetDlgItemTextW(window, IDC_DESKTOP_VALUE, desktop.c_str()); SetDlgItemTextW(window, IDC_MIC_VALUE, microphone.c_str());
}
void SettingsWindow::applyLiveGains(bool saveNow) {
    if (updatingControls_) return;
    const auto window = window_.load();
    auto settings = engine_.settings();
    const int desktop = static_cast<int>(SendDlgItemMessageW(window, IDC_DESKTOP_SLIDER, TBM_GETPOS, 0, 0));
    const int microphone = static_cast<int>(SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_GETPOS, 0, 0));
    if (settings.desktopPercent != desktop || settings.microphonePercent != microphone) {
        if (settings.desktopPercent != desktop) desktopMeter_.rescale(settings.desktopPercent ? static_cast<float>(desktop) / settings.desktopPercent : 0);
        if (settings.microphonePercent != microphone) microphoneMeter_.rescale(settings.microphonePercent ? static_cast<float>(microphone) / settings.microphonePercent : 0);
        settings.desktopPercent = desktop; settings.microphonePercent = microphone;
        engine_.apply(std::move(settings)); levelLabelTick_ = 0;
        gainsSavePending_ = true; gainsSaveFailed_ = false;
        if (!SetTimer(window, 3, 300, nullptr)) saveNow = true;
    }
    if (saveNow) saveLiveGains(); else updatePendingState();
}
void SettingsWindow::saveLiveGains() {
    const auto window = window_.load(); KillTimer(window, 3);
    if (!gainsSavePending_) return;
    if (saveSettings(settingsFile_, engine_.settings())) {
        gainsSavePending_ = gainsSaveFailed_ = false;
        if (notice_.key == Text::GainSaveError) { notice_.clear(); noticeUntil_ = 0; }
    }
    else {
        gainsSaveFailed_ = true;
        notice_ = Text::GainSaveError; noticeUntil_ = 0;
    }
    updatePendingState();
}
void SettingsWindow::updatePendingState() {
    if (updatingControls_) return;
    const auto window = window_.load();
    const auto applied = engine_.settings();
    auto current = draft();
    // Reflect an external mic hotkey unless the checkbox itself has a draft.
    if (current.microphoneMuted == appliedSettings_.microphoneMuted && applied.microphoneMuted != appliedSettings_.microphoneMuted) {
        CheckDlgButton(window, IDC_MIC_MUTE, applied.microphoneMuted ? BST_CHECKED : BST_UNCHECKED);
        current.microphoneMuted = applied.microphoneMuted;
    }
    const bool applicationsChanged = current.applications.size() != applied.applications.size() ||
        std::any_of(current.applications.begin(), current.applications.end(), [&](const auto& path) {
            return std::none_of(applied.applications.begin(), applied.applications.end(), [&](const auto& previous) { return samePath(path, previous); });
        });
    const bool sourceChanged = current.mode != applied.mode || applicationsChanged;
    const bool micChanged = current.microphoneMuted != applied.microphoneMuted;
    const bool updatesChanged = current.autoCheckUpdates != applied.autoCheckUpdates;
    pendingSettings_ = sourceChanged || micChanged || updatesChanged;
    appliedSettings_ = applied;
    const auto mark = [&](int id, Text key, bool changed) {
        const auto text = std::wstring(tr(key)) + (changed ? L" *" : L"");
        setControlText(window, id, text.c_str());
    };
    mark(IDC_SOURCE_GROUP, Text::SourceGroup, sourceChanged);
    mark(IDC_MIC_MUTE, Text::MicMute, micChanged);
    mark(IDC_AUTO_UPDATE, Text::AutoUpdate, updatesChanged);
    setControlText(window, IDC_SETTINGS_HINT, tr(pendingSettings_ ? Text::PendingHint :
        gainsSaveFailed_ ? Text::GainFailedHint : gainsSavePending_ ? Text::GainSavingHint : Text::SettingsHint));
    mark(IDC_APPLY, gainsSavePending_ && !pendingSettings_ ? Text::SaveVolume : Text::Apply, pendingSettings_);
    EnableWindow(GetDlgItem(window, IDC_APPLY), pendingSettings_ || gainsSavePending_);
    const bool warning = pendingSettings_ || gainsSaveFailed_;
    if (warning != headerWarning_) {
        headerWarning_ = warning;
        const auto normal = font_ ? font_ : reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        SendDlgItemMessageW(window, IDC_SETTINGS_HINT, WM_SETFONT, reinterpret_cast<WPARAM>(warning ? headerFont_ : normal), TRUE);
    }
}
void SettingsWindow::stopMetering() {
    const auto window = window_.load();
    KillTimer(window, 2); meterTimerRunning_ = false;
    engine_.setMeteringVisible(false);
    desktopMeter_.reset(); microphoneMeter_.reset();
    meterTick_ = GetTickCount64(); levelLabelTick_ = 0;
    for (int id : {IDC_DESKTOP_METER, IDC_MIC_METER}) SendDlgItemMessageW(window, id, PBM_SETPOS, 0, 0);
    for (int id : {IDC_DESKTOP_LEVEL, IDC_MIC_LEVEL}) SetDlgItemTextW(window, id, L"−∞ dBFS");
}
void SettingsWindow::refreshMetering() {
    const auto window = window_.load();
    const bool show = IsDlgButtonChecked(window, IDC_SHOW_METERS) == BST_CHECKED;
    for (int id : {IDC_DESKTOP_METER, IDC_MIC_METER, IDC_DESKTOP_LEVEL, IDC_MIC_LEVEL})
        ShowWindow(GetDlgItem(window, id), show ? SW_SHOW : SW_HIDE);
    const wchar_t* hint = show ? tr(Text::MeterHint) : tr(Text::MetersOff);
    wchar_t previous[64]{}; GetDlgItemTextW(window, IDC_METER_HINT, previous, 64);
    if (std::wstring(previous) != hint) SetDlgItemTextW(window, IDC_METER_HINT, hint);
    const bool run = show && IsWindowVisible(window) && !IsIconic(window) && engine_.requestedServer() != 0;
    if (run == meterTimerRunning_) return;
    if (!run) { stopMetering(); return; }
    stopMetering();
    engine_.setMeteringVisible(true);
    meterTimerRunning_ = SetTimer(window, 2, 33, nullptr) != 0;
    if (!meterTimerRunning_) {
        engine_.setMeteringVisible(false);
        SetDlgItemTextW(window, IDC_METER_HINT, tr(Text::MeterError) );
    }
}
void SettingsWindow::toggleMeters() {
    const auto window = window_.load();
    auto settings = engine_.settings();
    const bool previous = settings.showLevelMeters;
    settings.showLevelMeters = IsDlgButtonChecked(window, IDC_SHOW_METERS) == BST_CHECKED;
    // The display toggle takes effect immediately, without applying unrelated
    // gain/application drafts currently being edited in the dialog.
    if (saveSettings(settingsFile_, settings)) {
        engine_.apply(std::move(settings));
        KillTimer(window, 3); gainsSavePending_ = gainsSaveFailed_ = false;
        if (notice_.key == Text::GainSaveError) { notice_.clear(); noticeUntil_ = 0; }
    }
    else {
        CheckDlgButton(window, IDC_SHOW_METERS, previous ? BST_CHECKED : BST_UNCHECKED);
        notice_ = Text::MeterSaveError; noticeUntil_ = 0;
    }
    updateStatus();
}
void SettingsWindow::updateLevels() {
    const auto window = window_.load();
    if (!meterTimerRunning_) return;
    if (!engine_.requestedServer() || !IsWindowVisible(window) || IsIconic(window)) { stopMetering(); return; }
    const auto reading = engine_.takeLevels();
    const auto now = GetTickCount64();
    const float elapsed = static_cast<float>(now - meterTick_) / 1000.0f; meterTick_ = now;
    if (!reading.active || !IsWindowVisible(window) || IsIconic(window)) {
        desktopMeter_.reset(); microphoneMeter_.reset();
    }
    if (reading.microphoneMuted) microphoneMeter_.reset();
    const auto desktop = desktopMeter_.update(reading.peaks.desktop, elapsed);
    const auto microphone = microphoneMeter_.update(reading.peaks.microphone, elapsed);
    const bool updateReadouts = now - levelLabelTick_ >= 100;
    if (updateReadouts) levelLabelTick_ = now;
    const auto draw = [&](int meter, int label, float peak, bool overloaded) {
        const float db = peakDb(peak);
        const auto color = overloaded ? RGB(210, 67, 67) : db >= -12 ? RGB(196, 140, 32) : RGB(41, 151, 97);
        SendDlgItemMessageW(window, meter, PBM_SETBARCOLOR, 0, color);
        SendDlgItemMessageW(window, meter, PBM_SETPOS, meterPosition(peak), 0);
        if (!updateReadouts) return;
        std::wstring text = L"−∞ dBFS";
        if (peak > 0) {
            std::wostringstream value;
            const float rounded = std::round(db * 10) / 10;
            if (rounded > 0) value << L'+';
            value << std::fixed << std::setprecision(1) << (rounded == 0 ? 0.0f : rounded) << L" dBFS";
            text = value.str();
        }
        wchar_t previous[64]{}; GetDlgItemTextW(window, label, previous, 64);
        if (text != previous) SetDlgItemTextW(window, label, text.c_str());
    };
    draw(IDC_DESKTOP_METER, IDC_DESKTOP_LEVEL, desktop, desktopMeter_.overloaded());
    draw(IDC_MIC_METER, IDC_MIC_LEVEL, microphone, microphoneMeter_.overloaded());
}
void SettingsWindow::updateStatus() {
    const auto window = window_.load();
    if (noticeUntil_ && GetTickCount64() >= noticeUntil_) { notice_.clear(); noticeUntil_ = 0; }
    const auto status = engine_.status();
    const bool requested = engine_.requestedServer() != 0;
    EnableWindow(GetDlgItem(window, IDC_STOP), requested);
    EnableWindow(GetDlgItem(window, IDC_START), !requested);
    std::wstring text = notice_.empty() ? status.message.str() : notice_.str();
    if (IsDlgButtonChecked(window, IDC_DETAILS) == BST_CHECKED) {
        text += L"\n" + format(Text::Statistics, {std::to_wstring(status.sources), std::to_wstring(status.capturedFrames),
            std::to_wstring(status.callbacks), std::to_wstring(status.queuedFrames), std::to_wstring(status.dropped), std::to_wstring(status.trimmed)});
    }
    SetDlgItemTextW(window, IDC_STATUS, text.c_str());
    updatePendingState();
    refreshMetering();
}
void SettingsWindow::updateUpdateStatus() {
    const auto result = updates_.status(); const auto window = window_.load();
    SetDlgItemTextW(window, IDC_UPDATE_STATUS, result.message.str().c_str());
    EnableWindow(GetDlgItem(window, IDC_CHECK_UPDATE), result.phase != UpdatePhase::Checking);
}
void SettingsWindow::fitToScreen() {
    const auto window = window_.load();
    RECT bounds{}; GetWindowRect(window, &bounds);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    const int availableWidth = monitor.rcWork.right - monitor.rcWork.left - 32;
    const int availableHeight = monitor.rcWork.bottom - monitor.rcWork.top - 32;
    const double scale = std::min({1.0, static_cast<double>(availableWidth) / width, static_cast<double>(availableHeight) / height});
    LOGFONTW font{}; GetObjectW(reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)), sizeof(font), &font);
    if (scale < 1) {
        font.lfHeight = static_cast<LONG>(font.lfHeight * scale);
        font_ = CreateFontIndirectW(&font);
        for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            RECT rect{}; GetWindowRect(child, &rect); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
            SetWindowPos(child, nullptr, static_cast<int>(rect.left * scale), static_cast<int>(rect.top * scale),
                static_cast<int>((rect.right - rect.left) * scale), static_cast<int>((rect.bottom - rect.top) * scale), SWP_NOZORDER);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
    }
    font.lfWeight = FW_SEMIBOLD; font.lfHeight = static_cast<LONG>(font.lfHeight * 1.2);
    headerFont_ = CreateFontIndirectW(&font);
    SendDlgItemMessageW(window, IDC_HEADER, WM_SETFONT, reinterpret_cast<WPARAM>(headerFont_), TRUE);
    const int finalWidth = static_cast<int>(width * scale), finalHeight = static_cast<int>(height * scale);
    SetWindowPos(window, nullptr, monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - finalWidth) / 2,
        monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - finalHeight) / 2, finalWidth, finalHeight, SWP_NOZORDER);
    RECT listRect{}; const auto list = GetDlgItem(window, IDC_APPS); GetClientRect(list, &listRect);
    const int proportions[] = {37, 17, 46};
    for (int i = 0; i < 3; ++i) ListView_SetColumnWidth(list, i, (listRect.right - 20) * proportions[i] / 100);
}
}

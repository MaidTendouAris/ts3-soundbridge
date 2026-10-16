#include <Windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <ts3_functions.h>
#include <teamspeak/public_errors.h>
#include "windows/gui_resource.h"
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <stdexcept>

namespace {
std::atomic<bool> connected{false};
std::atomic<int> muted{0}, mutations{0};
int failures = 0;
std::string configDirectory;
unsigned int connection(uint64, int* value) { *value = connected.load() ? STATUS_CONNECTION_ESTABLISHED : STATUS_DISCONNECTED; return ERROR_ok; }
unsigned int clientValue(uint64, size_t flag, int* value) {
    if (flag != CLIENT_INPUT_MUTED) return ERROR_not_implemented;
    *value = muted.load(); return ERROR_ok;
}
unsigned int setClientValue(uint64, size_t, int) { ++mutations; return ERROR_ok; }
unsigned int setPreprocessor(uint64, const char*, const char*) { ++mutations; return ERROR_ok; }
uint64 currentServer() { return 42; }
void configPath(char* path, size_t length) { strncpy_s(path, length, configDirectory.c_str(), _TRUNCATE); }
void check(bool condition, const char* label) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
template<typename Function> Function symbol(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    if (!address) throw std::runtime_error(std::string("Missing plugin export: ") + name);
    return reinterpret_cast<Function>(address);
}
template<typename Predicate> bool wait(Predicate predicate) {
    for (int i = 0; i < 300; ++i) { if (predicate()) return true; Sleep(20); } return false;
}
HWND dialog() {
    HWND result = nullptr;
    EnumWindows([](HWND candidate, LPARAM data) -> BOOL {
        DWORD pid = 0; GetWindowThreadProcessId(candidate, &pid);
        wchar_t title[128]{}; GetWindowTextW(candidate, title, 128);
        if (pid == GetCurrentProcessId() && std::wstring(title) == L"TS3 SoundBridge · 设置") {
            *reinterpret_cast<HWND*>(data) = candidate; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result)); return result;
}
void click(HWND window, int id) { SendMessageW(GetDlgItem(window, id), BM_CLICK, 0, 0); }
std::wstring status(HWND window) {
    wchar_t text[2048]{}; GetDlgItemTextW(window, IDC_STATUS, text, 2048); return text;
}
bool screenshot(HWND window, const std::wstring& path) {
    RECT rect{}; GetWindowRect(window, &rect);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    const auto old = SelectObject(memory, bitmap);
    const BOOL rendered = PrintWindow(window, memory, 2);
    SelectObject(memory, old); ReleaseDC(window, screen); DeleteDC(memory);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token = 0;
    bool saved = false;
    if (rendered && Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok) {
        {
            Gdiplus::Bitmap image(bitmap, nullptr);
            // PNG encoder CLSID (GDI+ standard encoder).
            const CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
            saved = image.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
        }
        Gdiplus::GdiplusShutdown(token);
    }
    DeleteObject(bitmap); return saved;
}
}
int main(int argc, char** argv) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc < 2) return 2;
    bool capture = false, liveUpdates = false; std::filesystem::path imagePath;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--capture") capture = true;
        else if (std::string(argv[i]) == "--updates") liveUpdates = true;
        else if (std::string(argv[i]) == "--screenshot" && i + 1 < argc) imagePath = argv[++i];
    }
    const auto configRoot = std::filesystem::absolute(argv[1]).parent_path() / "plugin-test-config";
    std::filesystem::create_directories(configRoot);
    configDirectory = configRoot.string();
    const auto file = (configRoot / "TS3-SoundBridge.ini").wstring();
    DeleteFileW(file.c_str());
    HANDLE ini = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    const WORD bom = 0xFEFF; DWORD written = 0; WriteFile(ini, &bom, sizeof(bom), &written, nullptr); CloseHandle(ini);
    const auto appPath = (std::filesystem::current_path() / L"测试播放器.exe").wstring();
    const auto secondPath = (std::filesystem::current_path() / L"第二个播放器.exe").wstring();
    WritePrivateProfileStringW(L"Applications", L"Count", L"2", file.c_str());
    WritePrivateProfileStringW(L"Applications", L"App0", appPath.c_str(), file.c_str());
    WritePrivateProfileStringW(L"Applications", L"App1", secondPath.c_str(), file.c_str());
    WritePrivateProfileStringW(L"SoundBridge", L"AutoCheckUpdates", L"0", file.c_str());
    HMODULE module = LoadLibraryA(argv[1]);
    if (!module) { std::cerr << "LoadLibrary failed: " << GetLastError() << '\n'; return 1; }
    auto shutdown = symbol<void(*)()>(module, "ts3plugin_shutdown");
    try {
        const auto setFunctions = symbol<void(*)(TS3Functions)>(module, "ts3plugin_setFunctionPointers");
        const auto init = symbol<int(*)()>(module, "ts3plugin_init");
        const auto api = symbol<int(*)()>(module, "ts3plugin_apiVersion");
        const auto configure = symbol<void(*)(void*, void*)>(module, "ts3plugin_configure");
        const auto offers = symbol<int(*)()>(module, "ts3plugin_offersConfigure");
        const auto edit = symbol<void(*)(uint64, short*, int, int, int*)>(module, "ts3plugin_onEditCapturedVoiceDataEvent");
        const auto changed = symbol<void(*)(uint64)>(module, "ts3plugin_currentServerConnectionChanged");
        const auto disconnect = symbol<void(*)(uint64, int, unsigned int)>(module, "ts3plugin_onConnectStatusChangeEvent");
        const auto selfChanged = symbol<void(*)(uint64, int, const char*, const char*)>(module, "ts3plugin_onClientSelfVariableUpdateEvent");
        const auto menus = symbol<void(*)(PluginMenuItem***, char**)>(module, "ts3plugin_initMenus");
        const auto menu = symbol<void(*)(uint64, PluginMenuType, int, uint64)>(module, "ts3plugin_onMenuItemEvent");
        const auto freeMemory = symbol<void(*)(void*)>(module, "ts3plugin_freeMemory");
        check(!GetProcAddress(module, "ts3plugin_processCommand") && !GetProcAddress(module, "ts3plugin_commandKeyword"), "command configuration exports removed");
        setFunctions({}); check(init() == 1, "reject missing SDK functions");
        TS3Functions functions{};
        functions.getConnectionStatus = connection; functions.getClientSelfVariableAsInt = clientValue;
        functions.getCurrentServerConnectionHandlerID = currentServer; functions.getConfigPath = configPath;
        functions.setClientSelfVariableAsInt = setClientValue; functions.setPreProcessorConfigValue = setPreprocessor;
        setFunctions(functions);
        check(api() == 26 && offers() == 1 && init() == 0, "API 26 initialization and settings entry point");
        PluginMenuItem** items = nullptr; char* icon = nullptr; menus(&items, &icon);
        check(items && items[0] && items[1] && items[2] && items[3] && !items[4], "SDK-compatible menu array including manual updates");
        check(icon && std::string(icon) == "icon-32.png", "SVG-derived plugin menu icon");
        if (items) { for (int i = 0; items[i]; ++i) freeMemory(items[i]); freeMemory(items); } freeMemory(icon);
        short pcm = 10000; int flags = 2; edit(42, &pcm, 1, 1, &flags);
        check(pcm == 10000 && flags == 2, "inactive plugin leaves native voice untouched");
        configure(nullptr, nullptr);
        if (!wait([] { return dialog() && IsWindowVisible(dialog()); })) throw std::runtime_error("Settings GUI did not open");
        const auto window = dialog();
        check(SendDlgItemMessageW(window, IDC_DESKTOP_SLIDER, TBM_GETRANGEMAX, 0, 0) == 300 &&
              SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_GETRANGEMAX, 0, 0) == 300, "both GUI gains allow 300 percent");
        check(GetDlgItem(window, IDC_DESKTOP_METER) && GetDlgItem(window, IDC_MIC_METER), "separate shared-audio and microphone meters exist");
        check(IsDlgButtonChecked(window, IDC_SHOW_METERS) == BST_UNCHECKED && !IsWindowVisible(GetDlgItem(window, IDC_MIC_METER)),
            "meter display defaults off and bars are hidden");
        SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_SETPOS, TRUE, 250);
        click(window, IDC_SHOW_METERS);
        check(IsWindowVisible(GetDlgItem(window, IDC_MIC_METER)) && GetPrivateProfileIntW(L"SoundBridge", L"ShowLevelMeters", 0, file.c_str()) == 1,
            "display toggle takes effect and saves immediately");
        check(GetPrivateProfileIntW(L"SoundBridge", L"MicrophonePercent", 0, file.c_str()) == 100,
            "display toggle does not apply unrelated gain drafts");
        SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_SETPOS, TRUE, 100);
        check(SendDlgItemMessageW(window, IDC_DESKTOP_METER, PBM_GETPOS, 0, 0) == 0 &&
              SendDlgItemMessageW(window, IDC_MIC_METER, PBM_GETPOS, 0, 0) == 0, "inactive meters show silence");
        check(SendMessageW(window, WM_GETICON, ICON_BIG, 0) != 0, "SVG-derived window icon");
        check(IsDlgButtonChecked(window, IDC_AUTO_UPDATE) == BST_UNCHECKED, "saved update preference loaded");
        click(window, IDC_AUTO_UPDATE); click(window, IDC_APPLY);
        check(GetPrivateProfileIntW(L"SoundBridge", L"AutoCheckUpdates", 0, file.c_str()) == 1, "GUI automatic update preference saved");
        click(window, IDC_AUTO_UPDATE); click(window, IDC_APPLY);
        check(IsDlgButtonChecked(window, IDC_MODE_ALL) == BST_CHECKED && !IsWindowEnabled(GetDlgItem(window, IDC_APPS)), "all-audio mode disables application selection");
        click(window, IDC_MODE_SELECTED);
        check(IsWindowEnabled(GetDlgItem(window, IDC_APPS)), "GUI selected-application mode enables multi-selection");
        click(window, IDC_CLEAR); click(window, IDC_APPLY);
        check(status(window).find(L"至少一个") != std::wstring::npos, "GUI rejects empty selected-application list");
        const auto list = GetDlgItem(window, IDC_APPS);
        int selectedRow = -1, secondRow = -1;
        for (int row = 0; row < ListView_GetItemCount(list); ++row) {
            wchar_t path[32768]{}; ListView_GetItemText(list, row, 2, path, 32768);
            if (path == appPath) selectedRow = row;
            if (path == secondPath) secondRow = row;
        }
        if (selectedRow < 0 || secondRow < 0) throw std::runtime_error("Saved application was lost by refresh");
        ListView_SetCheckState(list, selectedRow, TRUE); ListView_SetCheckState(list, secondRow, TRUE); click(window, IDC_APPLY);
        check(GetPrivateProfileIntW(L"SoundBridge", L"Mode", 0, file.c_str()) == 1, "GUI selected mode persisted");
        check(GetPrivateProfileIntW(L"Applications", L"Count", 0, file.c_str()) == 2, "multiple GUI application checkboxes saved together");
        click(window, IDC_MODE_ALL);
        SendDlgItemMessageW(window, IDC_DESKTOP_SLIDER, TBM_SETPOS, TRUE, 300);
        SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_SETPOS, TRUE, 300);
        SendMessageW(window, WM_HSCROLL, 0, 0); click(window, IDC_APPLY);
        check(GetPrivateProfileIntW(L"SoundBridge", L"DesktopPercent", 0, file.c_str()) == 300 &&
              GetPrivateProfileIntW(L"SoundBridge", L"MicrophonePercent", 0, file.c_str()) == 300, "GUI saves expanded gains without clamping at 200 percent");
        SendDlgItemMessageW(window, IDC_DESKTOP_SLIDER, TBM_SETPOS, TRUE, 0);
        SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_SETPOS, TRUE, 50);
        SendMessageW(window, WM_HSCROLL, 0, 0); click(window, IDC_APPLY);
        check(GetPrivateProfileIntW(L"SoundBridge", L"MicrophonePercent", 0, file.c_str()) == 50, "GUI gain persisted");
        click(window, IDC_START);
        check(wait([&] { return status(window).find(L"先连接") != std::wstring::npos; }), "disconnected start reported in GUI");
        if (capture) {
            connected.store(true); muted.store(1); click(window, IDC_START);
            check(wait([&] { return status(window).find(L"静音") != std::wstring::npos; }), "muted start rejected in GUI");
            muted.store(0); click(window, IDC_START);
            const auto active = [&] { short value = 10000; int bits = 2; edit(42, &value, 1, 1, &bits); return value == 5000 && bits == 3; };
            if (!wait(active)) throw std::runtime_error("Real capture did not start against mocked SDK");
            SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_SETPOS, TRUE, 300); click(window, IDC_APPLY);
            check(wait([&] {
                short value = 16000; int bits = 2; edit(42, &value, 1, 1, &bits);
                wchar_t readout[64]{}; GetDlgItemTextW(window, IDC_MIC_LEVEL, readout, 64);
                return value == 32767 && SendDlgItemMessageW(window, IDC_MIC_METER, PBM_GETPOS, 0, 0) == 600 && readout[0] == L'+';
            }), "real callback applies 300 percent and meter exposes pre-clipping overload");
            wchar_t level[64]{}; GetDlgItemTextW(window, IDC_MIC_LEVEL, level, 64);
            check(level[0] == L'+' && SendDlgItemMessageW(window, IDC_DESKTOP_METER, PBM_GETPOS, 0, 0) == 0,
                "microphone readout is positive dBFS while zero-gain shared meter remains silent");
            click(window, IDC_SHOW_METERS);
            short unaffected = 16000; int unaffectedBits = 2; edit(42, &unaffected, 1, 1, &unaffectedBits);
            check(!IsWindowVisible(GetDlgItem(window, IDC_MIC_METER)) && SendDlgItemMessageW(window, IDC_MIC_METER, PBM_GETPOS, 0, 0) == 0 &&
                unaffected == 32767 && GetPrivateProfileIntW(L"SoundBridge", L"ShowLevelMeters", 1, file.c_str()) == 0,
                "disabling display clears/hides meters and preserves active gain/audio");
            click(window, IDC_SHOW_METERS);
            ShowWindow(window, SW_MINIMIZE);
            check(wait([&] { return IsIconic(window) && SendDlgItemMessageW(window, IDC_MIC_METER, PBM_GETPOS, 0, 0) == 0; }),
                "minimizing suspends and clears meter display");
            unaffected = 16000; unaffectedBits = 2; edit(42, &unaffected, 1, 1, &unaffectedBits);
            check(unaffected == 32767, "minimizing preserves audio gain and sharing");
            ShowWindow(window, SW_RESTORE);
            check(wait([&] {
                short value = 16000; int bits = 2; edit(42, &value, 1, 1, &bits);
                return SendDlgItemMessageW(window, IDC_MIC_METER, PBM_GETPOS, 0, 0) > 0;
            }), "restoring resumes metering without restarting audio");
            click(window, IDC_MIC_MUTE); click(window, IDC_APPLY);
            check(wait([&] { return SendDlgItemMessageW(window, IDC_MIC_METER, PBM_GETPOS, 0, 0) == 0; }), "independent mute clears microphone meter immediately");
            click(window, IDC_MIC_MUTE);
            SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_SETPOS, TRUE, 50); click(window, IDC_APPLY);
            click(window, IDC_MODE_SELECTED); click(window, IDC_APPLY);
            check(wait([&] { return status(window).find(L"等待所选应用") != std::wstring::npos; }), "active mode switch releases all-audio capture and waits for selected apps");
            click(window, IDC_MODE_ALL); click(window, IDC_APPLY);
            check(wait(active), "mode switch restarts all-audio capture without another start action");
            pcm = 10000; flags = 0; edit(42, &pcm, 1, 1, &flags);
            check(pcm == 0 && flags == 1, "closed microphone gate preserved");
            pcm = 10000; flags = 2; edit(99, &pcm, 1, 1, &flags);
            check(pcm == 10000 && flags == 2, "other server never receives mixing");
            SendMessageW(window, WM_CLOSE, 0, 0);
            check(!IsWindowVisible(window) && active() && SendDlgItemMessageW(window, IDC_MIC_METER, PBM_GETPOS, 0, 0) == 0,
                "closing GUI suspends meters while keeping sharing active");
            configure(nullptr, nullptr); check(wait([&] { return IsWindowVisible(window); }), "reopening reuses settings GUI");
            click(window, IDC_MIC_MUTE); click(window, IDC_APPLY);
            pcm = 10000; flags = 2; edit(42, &pcm, 1, 1, &flags);
            check(pcm == 0 && flags == 1, "GUI independent mic mute");
            selfChanged(42, CLIENT_INPUT_MUTED, "0", "1");
            pcm = 10000; flags = 2; edit(42, &pcm, 1, 1, &flags);
            check(pcm == 10000 && flags == 2, "native mute stops mixing immediately");
            menu(42, PLUGIN_MENU_TYPE_GLOBAL, 2, 0); changed(99);
            pcm = 10000; flags = 2; edit(42, &pcm, 1, 1, &flags);
            check(pcm == 10000 && flags == 2, "tab switch stops sharing");
            menu(42, PLUGIN_MENU_TYPE_GLOBAL, 2, 0); disconnect(42, STATUS_DISCONNECTED, 0);
            pcm = 10000; flags = 2; edit(42, &pcm, 1, 1, &flags);
            check(pcm == 10000 && flags == 2, "disconnect stops sharing");
            check(wait([&] { return SendDlgItemMessageW(window, IDC_MIC_METER, PBM_GETPOS, 0, 0) == 0 &&
                SendDlgItemMessageW(window, IDC_DESKTOP_METER, PBM_GETPOS, 0, 0) == 0; }), "disconnect clears both meters");
        }
        menu(42, PLUGIN_MENU_TYPE_GLOBAL, 3, 0);
        if (liveUpdates) {
            click(window, IDC_CHECK_UPDATE);
            check(wait([&] { wchar_t text[1024]{}; GetDlgItemTextW(window, IDC_UPDATE_STATUS, text, 1024);
                const std::wstring result = text; return result.find(L"最新正式版本") != std::wstring::npos || result.find(L"尚无公开") != std::wstring::npos; }), "GUI manual update button reaches live GitHub release result");
        }
        if (!imagePath.empty()) {
            click(window, capture ? IDC_MODE_ALL : IDC_MODE_SELECTED); click(window, IDC_DETAILS);
            SendDlgItemMessageW(window, IDC_DESKTOP_SLIDER, TBM_SETPOS, TRUE, 50);
            SendDlgItemMessageW(window, IDC_MIC_SLIDER, TBM_SETPOS, TRUE, 100);
            CheckDlgButton(window, IDC_MIC_MUTE, BST_UNCHECKED);
            SendMessageW(window, WM_HSCROLL, 0, 0); click(window, IDC_APPLY); Sleep(100);
            if (capture) {
                connected.store(true); muted.store(0); click(window, IDC_START);
                check(wait([&] {
                    short value = 10000; int bits = 2; edit(42, &value, 1, 1, &bits); return (bits & 1) != 0;
                }), "capture active for live meter screenshot");
                // Drive the production callback at its normal frame cadence.
                // An optional sibling probe can provide a short desktop test tone.
                for (int i = 0; i < 50; ++i) {
                    short values[480]; std::fill_n(values, 480, static_cast<short>(10000));
                    int bits = 2; edit(42, values, 480, 1, &bits); Sleep(10);
                }
                check(SendDlgItemMessageW(window, IDC_MIC_METER, PBM_GETPOS, 0, 0) > 0, "live microphone meter visible");
            }
            check(screenshot(window, std::filesystem::absolute(imagePath).wstring()), "GUI screenshot rendered");
            if (capture) click(window, IDC_STOP);
        }
        shutdown(); check(!IsWindow(window), "shutdown joins GUI and destroys window");
        check(init() == 0, "reload after GUI shutdown"); configure(nullptr, nullptr);
        check(wait([] { return dialog() && IsWindowVisible(dialog()); }), "GUI opens after plugin reload");
        check(IsDlgButtonChecked(dialog(), IDC_SHOW_METERS) == BST_CHECKED, "display preference survives plugin reload");
        check(SendDlgItemMessageW(dialog(), IDC_MIC_SLIDER, TBM_GETPOS, 0, 0) == GetPrivateProfileIntW(L"SoundBridge", L"MicrophonePercent", 0, file.c_str()), "saved settings restored by plugin reload");
        pcm = 10000; flags = 2; edit(42, &pcm, 1, 1, &flags);
        check(pcm == 10000 && flags == 2, "reloaded plugin does not auto-start sharing");
        check(mutations.load() == 0, "native capture/transmission settings never modified");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; ++failures; }
    shutdown(); FreeLibrary(module);
    DeleteFileW(file.c_str());
    if (failures) return 1;
    std::cout << "All plugin contract, GUI and lifecycle tests passed (mock TS3 SDK; no server connection).\n";
    return 0;
}

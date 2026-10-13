#include "bridge_engine.h"
#include "windows/settings_window.h"
#include "soundbridge_version.h"
#include <ts3_functions.h>
#include <teamspeak/public_errors.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

#define SB_EXPORT extern "C" __declspec(dllexport)
namespace {
TS3Functions ts3{};
std::mutex lifecycle;
std::unique_ptr<soundbridge::BridgeEngine> engine;
std::unique_ptr<soundbridge::SettingsWindow> window;
std::unique_ptr<soundbridge::UpdateChecker> updates;
std::wstring settingsFile;
std::string pluginId;
bool validateServer(std::uint64_t server, std::wstring& error) {
    int status = 0, muted = 1;
    if (!server || ts3.getConnectionStatus(server, &status) != ERROR_ok || status != STATUS_CONNECTION_ESTABLISHED) {
        error = L"请先连接 TS3 服务器，再开始共享。"; return false;
    }
    if (ts3.getClientSelfVariableAsInt(server, CLIENT_INPUT_MUTED, &muted) != ERROR_ok || muted) {
        error = L"TS3 麦克风已静音，共享停止。取消静音后请手动开始。"; return false;
    }
    return true;
}
std::wstring configFile() {
    char path[4096]{}; ts3.getConfigPath(path, sizeof(path));
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, result.data(), length);
    result.pop_back();
    if (result.back() != L'\\' && result.back() != L'/') result += L'\\';
    return result + L"TS3-SoundBridge.ini";
}
}

SB_EXPORT const char* ts3plugin_name() { return "TS3 SoundBridge"; }
SB_EXPORT const char* ts3plugin_version() { return SOUNDBRIDGE_VERSION_STRING; }
SB_EXPORT int ts3plugin_apiVersion() { return 26; }
SB_EXPORT const char* ts3plugin_author() { return "MaidTendouAris"; }
SB_EXPORT const char* ts3plugin_description() {
    return "通过设置窗口共享全部非 TS3 声音，或同时共享多个指定应用；保留麦克风原有发言规则。";
}
SB_EXPORT void ts3plugin_setFunctionPointers(const TS3Functions functions) { ts3 = functions; }
SB_EXPORT int ts3plugin_init() {
    std::lock_guard<std::mutex> lock(lifecycle);
    if (!ts3.getConnectionStatus || !ts3.getClientSelfVariableAsInt || !ts3.getCurrentServerConnectionHandlerID || !ts3.getConfigPath) return 1;
    if (engine) return 0;
    try {
        settingsFile = configFile();
        if (settingsFile.empty()) return 1;
        engine = std::make_unique<soundbridge::BridgeEngine>(soundbridge::loadSettings(settingsFile), validateServer);
        updates = std::make_unique<soundbridge::UpdateChecker>(settingsFile, engine->settings().autoCheckUpdates, [](const auto& result) {
            if (ts3.printMessageToCurrentTab) {
                const std::string message = "[SoundBridge] 发现新版 " + result.version + "，请在设置窗口打开 GitHub Releases 下载页。";
                ts3.printMessageToCurrentTab(message.c_str());
            }
        });
        window = std::make_unique<soundbridge::SettingsWindow>(*engine, *updates, settingsFile, [] { return ts3.getCurrentServerConnectionHandlerID(); });
        if (ts3.printMessageToCurrentTab) ts3.printMessageToCurrentTab("[SoundBridge] 已加载，共享默认关闭。通过插件菜单或插件管理页的“设置”打开窗口。");
        return 0;
    } catch (...) { window.reset(); updates.reset(); engine.reset(); return 1; }
}
SB_EXPORT void ts3plugin_shutdown() {
    std::lock_guard<std::mutex> lock(lifecycle);
    if (engine) engine->stop();
    window.reset(); updates.reset(); engine.reset(); pluginId.clear();
}
SB_EXPORT void ts3plugin_registerPluginID(const char* id) { pluginId = id ? id : ""; }
SB_EXPORT void ts3plugin_freeMemory(void* memory) { std::free(memory); }
SB_EXPORT int ts3plugin_offersConfigure() { return 1; } // PLUGIN_OFFERS_CONFIGURE_NEW_THREAD
SB_EXPORT void ts3plugin_configure(void*, void*) {
    std::lock_guard<std::mutex> lock(lifecycle);
    if (window) window->show();
}
SB_EXPORT void ts3plugin_onEditCapturedVoiceDataEvent(uint64 server, short* samples, int frames, int channels, int* edited) {
    std::unique_lock<std::mutex> lock(lifecycle, std::try_to_lock);
    if (lock.owns_lock() && engine) engine->edit(server, samples, frames, channels, edited);
}
SB_EXPORT void ts3plugin_onConnectStatusChangeEvent(uint64 server, int status, unsigned int) {
    std::lock_guard<std::mutex> lock(lifecycle);
    if (engine && status != STATUS_CONNECTION_ESTABLISHED) engine->stopFor(server);
}
SB_EXPORT void ts3plugin_currentServerConnectionChanged(uint64 server) {
    std::lock_guard<std::mutex> lock(lifecycle);
    if (engine) engine->serverChanged(server);
}
SB_EXPORT void ts3plugin_onClientSelfVariableUpdateEvent(uint64 server, int flag, const char*, const char* value) {
    if (flag != CLIENT_INPUT_MUTED || (value && std::strcmp(value, "0") == 0)) return;
    std::lock_guard<std::mutex> lock(lifecycle);
    if (engine) engine->stopFor(server);
}
SB_EXPORT void ts3plugin_initMenus(PluginMenuItem*** items, char** icon) {
    *icon = _strdup("icon-32.png");
    *items = static_cast<PluginMenuItem**>(std::calloc(5, sizeof(PluginMenuItem*)));
    if (!*items) return;
    const char* labels[] = { "SoundBridge 设置…", "开始音频共享", "停止音频共享", "检查更新…" };
    for (int i = 0; i < 4; ++i) {
        (*items)[i] = static_cast<PluginMenuItem*>(std::calloc(1, sizeof(PluginMenuItem)));
        if (!(*items)[i]) break;
        (*items)[i]->type = PLUGIN_MENU_TYPE_GLOBAL; (*items)[i]->id = i + 1;
        strcpy_s((*items)[i]->text, labels[i]);
        if (i == 0) strcpy_s((*items)[i]->icon, "icon-32.png");
    }
}
SB_EXPORT void ts3plugin_onMenuItemEvent(uint64 server, PluginMenuType type, int item, uint64) {
    if (type != PLUGIN_MENU_TYPE_GLOBAL) return;
    std::lock_guard<std::mutex> lock(lifecycle);
    if (!engine || !window) return;
    if (item == 1) window->show();
    else if (item == 2) engine->start(server);
    else if (item == 3) engine->stop();
    else if (item == 4 && updates) { window->show(); updates->checkNow(); }
}
SB_EXPORT void ts3plugin_initHotkeys(PluginHotkey*** keys) {
    *keys = static_cast<PluginHotkey**>(std::calloc(4, sizeof(PluginHotkey*)));
    if (!*keys) return;
    const char* names[] = { "toggle-share", "toggle-mic", "open-settings" };
    const char* labels[] = { "开始 / 停止音频共享", "切换麦克风独立静音", "打开 SoundBridge 设置" };
    for (int i = 0; i < 3; ++i) {
        (*keys)[i] = static_cast<PluginHotkey*>(std::calloc(1, sizeof(PluginHotkey)));
        if (!(*keys)[i]) break;
        strcpy_s((*keys)[i]->keyword, names[i]); strcpy_s((*keys)[i]->description, labels[i]);
    }
}
SB_EXPORT void ts3plugin_onHotkeyEvent(const char* keyword) {
    if (!keyword) return;
    std::lock_guard<std::mutex> lock(lifecycle);
    if (!engine || !window) return;
    if (std::strcmp(keyword, "open-settings") == 0) window->show();
    else if (std::strcmp(keyword, "toggle-mic") == 0) {
        engine->toggleMicrophone(); soundbridge::saveSettings(settingsFile, engine->settings());
    } else if (std::strcmp(keyword, "toggle-share") == 0) {
        if (engine->requestedServer()) engine->stop(); else engine->start(ts3.getCurrentServerConnectionHandlerID());
    }
}

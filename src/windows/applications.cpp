#include "applications.h"
#include <TlHelp32.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl.h>
#include <algorithm>
#include <set>
#include <map>

namespace soundbridge {
using Microsoft::WRL::ComPtr;
bool samePath(const std::wstring& first, const std::wstring& second) {
    return CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) == CSTR_EQUAL;
}
bool queryProcess(DWORD pid, ProcessInfo& process) {
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle) return false;
    std::vector<wchar_t> path(32768); DWORD length = static_cast<DWORD>(path.size());
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool success = QueryFullProcessImageNameW(handle, 0, path.data(), &length) &&
        GetProcessTimes(handle, &created, &exited, &kernel, &user);
    CloseHandle(handle);
    if (!success) return false;
    process.pid = pid;
    process.created = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    process.path.assign(path.data(), length);
    process.name = process.path.substr(process.path.find_last_of(L"\\/") + 1);
    return true;
}
std::vector<ProcessInfo> processSnapshot(const std::vector<std::wstring>& matchingPaths) {
    std::vector<ProcessInfo> processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return processes;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) do {
        ProcessInfo process;
        process.pid = entry.th32ProcessID; process.parent = entry.th32ParentProcessID;
        process.name = entry.szExeFile;
        for (const auto& path : matchingPaths) {
            const auto name = path.substr(path.find_last_of(L"\\/") + 1);
            if (samePath(name, process.name)) { queryProcess(process.pid, process); break; }
        }
        processes.push_back(std::move(process));
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return processes;
}
namespace {
bool ancestor(DWORD parent, DWORD child, const std::map<DWORD, const ProcessInfo*>& byPid) {
    std::set<DWORD> visited;
    while (child && visited.insert(child).second) {
        if (child == parent) return true;
        const auto found = byPid.find(child);
        if (found == byPid.end()) return false;
        child = found->second->parent;
    }
    return false;
}
}
std::vector<ProcessInfo> selectRoots(const std::vector<ProcessInfo>& processes,
    const std::vector<std::wstring>& selectedPaths, DWORD excludedPid, std::wstring& error) {
    error.clear();
    std::map<DWORD, const ProcessInfo*> byPid;
    for (const auto& process : processes) byPid[process.pid] = &process;
    std::vector<ProcessInfo> candidates;
    for (const auto& process : processes) {
        if (!process.pid || process.path.empty() || !process.created) continue;
        for (const auto& path : selectedPaths) if (samePath(path, process.path)) {
            if (ancestor(process.pid, excludedPid, byPid) || ancestor(excludedPid, process.pid, byPid)) {
                error = L"不能共享 TeamSpeak 或包含 TeamSpeak 的进程树。"; return {};
            }
            candidates.push_back(process); break;
        }
    }
    std::vector<ProcessInfo> roots;
    for (const auto& process : candidates) {
        bool covered = false;
        for (const auto& other : candidates) {
            if (process.pid != other.pid && other.created <= process.created && ancestor(other.pid, process.pid, byPid)) {
                covered = true; break;
            }
        }
        if (!covered && std::none_of(roots.begin(), roots.end(), [&](const auto& root) { return root.pid == process.pid; })) roots.push_back(process);
    }
    std::sort(roots.begin(), roots.end(), [](const auto& first, const auto& second) { return first.pid < second.pid; });
    if (roots.size() > 16) { error = L"所选应用超过 16 个独立进程树，请减少选择。"; return {}; }
    return roots;
}
std::vector<AudioApplication> audioApplications(DWORD excludedPid) {
    std::vector<AudioApplication> applications;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) return applications;
    struct ApartmentScope {
        HRESULT result;
        ~ApartmentScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartmentScope{apartment};
    const auto processes = processSnapshot({});
    std::map<DWORD, const ProcessInfo*> byPid;
    for (const auto& process : processes) byPid[process.pid] = &process;
    ProcessInfo excluded;
    queryProcess(excludedPid, excluded);
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> devices;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
        SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
        UINT count = 0; devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device; ComPtr<IAudioSessionManager2> manager; ComPtr<IAudioSessionEnumerator> sessions;
            if (FAILED(devices->Item(i, &device)) || FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager)) ||
                FAILED(manager->GetSessionEnumerator(&sessions))) continue;
            int sessionCount = 0; sessions->GetCount(&sessionCount);
            for (int j = 0; j < sessionCount; ++j) {
                ComPtr<IAudioSessionControl> session; ComPtr<IAudioSessionControl2> details;
                if (FAILED(sessions->GetSession(j, &session)) || FAILED(session.As(&details)) || details->IsSystemSoundsSession() == S_OK) continue;
                AudioSessionState state = AudioSessionStateExpired; DWORD pid = 0;
                if (FAILED(details->GetProcessId(&pid)) || !pid || FAILED(session->GetState(&state)) || state == AudioSessionStateExpired) continue;
                ProcessInfo process;
                if (!queryProcess(pid, process) || samePath(process.path, excluded.path) ||
                    ancestor(excludedPid, pid, byPid) || ancestor(pid, excludedPid, byPid)) continue;
                auto found = std::find_if(applications.begin(), applications.end(), [&](const auto& app) { return samePath(app.path, process.path); });
                if (found == applications.end()) { applications.push_back({process.path, process.name, {}, false}); found = applications.end() - 1; }
                if (std::find(found->pids.begin(), found->pids.end(), pid) == found->pids.end()) found->pids.push_back(pid);
                found->playing = found->playing || state == AudioSessionStateActive;
            }
        }
    }
    std::sort(applications.begin(), applications.end(), [](const auto& first, const auto& second) { return first.name < second.name; });
    return applications;
}
}

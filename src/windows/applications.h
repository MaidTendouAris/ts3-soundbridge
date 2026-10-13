#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace soundbridge {
struct ProcessInfo {
    DWORD pid = 0, parent = 0;
    std::uint64_t created = 0;
    std::wstring path, name;
};
struct AudioApplication {
    std::wstring path, name;
    std::vector<DWORD> pids;
    bool playing = false;
};
bool samePath(const std::wstring& first, const std::wstring& second);
bool queryProcess(DWORD pid, ProcessInfo& process);
std::vector<ProcessInfo> processSnapshot(const std::vector<std::wstring>& matchingPaths);
std::vector<AudioApplication> audioApplications(DWORD excludedPid);
// Pure resolver: deduplicate overlapping process trees and reject any tree
// containing this TS3 process. Errors never fall back to whole-system capture.
std::vector<ProcessInfo> selectRoots(const std::vector<ProcessInfo>& processes,
    const std::vector<std::wstring>& selectedPaths, DWORD excludedPid, std::wstring& error);
}

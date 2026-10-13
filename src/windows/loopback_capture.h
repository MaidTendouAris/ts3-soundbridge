#pragma once
#include "audio_buffer.h"
#include <Windows.h>
#include <atomic>
#include <thread>

namespace soundbridge {
class LoopbackCapture {
public:
    explicit LoopbackCapture(AudioBuffer<>& buffer) : buffer_(buffer) {}
    ~LoopbackCapture() { stop(); }
    LoopbackCapture(const LoopbackCapture&) = delete;
    LoopbackCapture& operator=(const LoopbackCapture&) = delete;
    // Control-thread methods only; never called by the TS3 audio callback.
    HRESULT start(DWORD targetPid, bool includeProcessTree = false);
    void stop() noexcept;
    bool running() const noexcept { return running_.load(); }
    HRESULT error() const noexcept { return error_.load(); }
    std::uint64_t frames() const noexcept { return frames_.load(); }
    std::uint64_t discontinuities() const noexcept { return discontinuities_.load(); }
private:
    void run(DWORD targetPid, bool includeProcessTree) noexcept;
    AudioBuffer<>& buffer_;
    HANDLE stopEvent_ = nullptr, readyEvent_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<HRESULT> error_{S_OK};
    std::atomic<std::uint64_t> frames_{0}, discontinuities_{0};
};
}

#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace soundbridge {
struct StereoFrame { float left = 0; float right = 0; };

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // intentional cache-line padding of the SPSC indices
#endif

// Exactly one capture producer and one audio consumer. Control code may reset
// only after stopping the producer and excluding the audio callback.
template<std::size_t Capacity = 16384>
class AudioBuffer {
public:
    bool push(StereoFrame frame) noexcept {
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) >= Capacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        frames_[write % Capacity] = frame;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    bool pop(StereoFrame& frame) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        frame = frames_[read % Capacity];
        read_.store(read + 1, std::memory_order_release);
        return true;
    }
    std::size_t size() const noexcept {
        const auto read = read_.load(std::memory_order_acquire);
        const auto write = write_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(write - read);
    }
    // Consumer alone discards old audio, bounding latency after callback stalls.
    void keepNewest(std::size_t count) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        const auto write = write_.load(std::memory_order_acquire);
        if (write - read > count) {
            trimmed_.fetch_add(write - read - count, std::memory_order_relaxed);
            read_.store(write - count, std::memory_order_release);
        }
    }
    // After overflow, even the newest queued frame can be old: the producer
    // could not enqueue recent audio while full. Discard that entire queue.
    // A subsequent packet repopulates it with current audio.
    void recoverFromOverflow() noexcept {
        const auto dropped = dropped_.load(std::memory_order_acquire);
        if (dropped != observedDrops_) {
            keepNewest(0);
            observedDrops_ = dropped;
        }
    }
    void reset() noexcept {
        read_.store(0); write_.store(0); dropped_.store(0); trimmed_.store(0);
        observedDrops_ = 0;
    }
    std::uint64_t dropped() const noexcept { return dropped_.load(); }
    std::uint64_t trimmed() const noexcept { return trimmed_.load(); }
private:
    std::array<StereoFrame, Capacity> frames_{};
    alignas(64) std::atomic<std::uint64_t> read_{0};
    alignas(64) std::atomic<std::uint64_t> write_{0};
    std::atomic<std::uint64_t> dropped_{0}, trimmed_{0};
    std::uint64_t observedDrops_ = 0; // audio consumer only
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

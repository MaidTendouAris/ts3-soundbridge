#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace soundbridge {
struct AudioPeaks { float desktop = 0, microphone = 0; };

// One audio producer, one GUI consumer. Preserve short transients between GUI
// refreshes without locks, allocation, or window calls in the audio callback.
class PeakAccumulator {
public:
    void publish(AudioPeaks peaks) noexcept {
        maximum(desktop_, peaks.desktop); maximum(microphone_, peaks.microphone);
    }
    AudioPeaks take() noexcept {
        return {desktop_.exchange(0, std::memory_order_relaxed), microphone_.exchange(0, std::memory_order_relaxed)};
    }
    void reset() noexcept { take(); }
private:
    static void maximum(std::atomic<float>& value, float peak) noexcept {
        auto previous = value.load(std::memory_order_relaxed);
        while (peak > previous && !value.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {}
    }
    static_assert(std::atomic<float>::is_always_lock_free, "Audio peak publication must be lock-free");
    std::atomic<float> desktop_{0}, microphone_{0};
};

// Both meters share a -60..0 dBFS scale. Retain values above full scale so the
// numeric readout and red indication expose overload before final PCM clipping.
inline float peakDb(float peak) noexcept {
    return peak > 0 && std::isfinite(peak) ? 20.0f * std::log10(peak) : -INFINITY;
}
inline int meterPosition(float peak) noexcept {
    return static_cast<int>(std::lround(std::clamp(peakDb(peak), -60.0f, 0.0f) * 10 + 600));
}
class MeterEnvelope {
public:
    float update(float peak, float elapsedSeconds) noexcept {
        if (!std::isfinite(peak) || peak < 0) peak = 0;
        value_ = std::max(peak, value_ * std::pow(10.0f, -24.0f * std::clamp(elapsedSeconds, 0.0f, 5.0f) / 20.0f));
        if (peak == 0 && value_ < 0.001f) value_ = 0;
        return value_;
    }
    void reset() noexcept { value_ = 0; }
private:
    float value_ = 0;
};
}

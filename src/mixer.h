#pragma once
#include "audio_buffer.h"
#include "audio_levels.h"
#include <cmath>

namespace soundbridge {
inline short toPcm16(float value) noexcept {
    if (!std::isfinite(value)) return 0;
    value = std::clamp(value, -1.0f, 32767.0f / 32768.0f);
    return static_cast<short>(std::lrint(value * 32768.0f));
}
struct MixResult {
    std::size_t desktopFrames = 0, underrunFrames = 0;
    AudioPeaks peaks;
};

template<std::size_t Capacity>
MixResult mixSources(AudioBuffer<Capacity>* const* buffers, std::size_t sourceCount, short* samples, int frames, int channels,
              int& flags, float desktopGain, float microphoneGain, bool microphoneMuted, bool measureLevels = true) noexcept {
    MixResult result;
    if (!samples || frames <= 0 || (channels != 1 && channels != 2)) return result;
    // TS3's ORIGINAL send bit gates the microphone before desktop audio changes it.
    const bool microphoneAllowed = (flags & 2) != 0 && !microphoneMuted;
    bool desktopAudible = false;
    for (std::size_t source = 0; source < sourceCount; ++source) {
        buffers[source]->recoverFromOverflow();
        if (buffers[source]->size() > 4800) buffers[source]->keepNewest(1920);
    }
    for (int i = 0; i < frames; ++i) {
        StereoFrame desktop;
        bool available = false;
        for (std::size_t source = 0; source < sourceCount; ++source) {
            StereoFrame frame;
            if (buffers[source]->pop(frame)) {
                desktop.left += frame.left; desktop.right += frame.right; available = true;
            }
        }
        if (available) ++result.desktopFrames; else ++result.underrunFrames;
        const float left = desktop.left * desktopGain;
        const float right = desktop.right * desktopGain;
        if (std::abs(left) >= 1.0f / 32768 || std::abs(right) >= 1.0f / 32768)
            desktopAudible = true;
        for (int channel = 0; channel < channels; ++channel) {
            const auto index = i * channels + channel;
            const float microphone = microphoneAllowed ? samples[index] / 32768.0f * microphoneGain : 0;
            const float system = channels == 1 ? (left + right) * 0.5f : (channel == 0 ? left : right);
            if (measureLevels) {
                result.peaks.microphone = std::max(result.peaks.microphone, std::abs(microphone));
                result.peaks.desktop = std::max(result.peaks.desktop, std::abs(system));
            }
            samples[index] = toPcm16(microphone + system);
        }
    }
    flags |= 1;
    if (desktopAudible) flags |= 2;
    else if (!microphoneAllowed) flags &= ~2;
    return result;
}
template<std::size_t Capacity>
MixResult mix(AudioBuffer<Capacity>& buffer, short* samples, int frames, int channels,
              int& flags, float desktopGain, float microphoneGain, bool microphoneMuted, bool measureLevels = true) noexcept {
    AudioBuffer<Capacity>* sources[] = { &buffer };
    return mixSources(sources, 1, samples, frames, channels, flags, desktopGain, microphoneGain, microphoneMuted, measureLevels);
}
}

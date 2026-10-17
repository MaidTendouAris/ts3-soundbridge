#include "mixer.h"
#include <iostream>
#include <thread>
#include <limits>
#include <vector>

int failures = 0;
void check(bool condition, const char* text) {
    if (!condition) { std::cerr << "FAIL: " << text << '\n'; ++failures; }
}
int main() {
    using namespace soundbridge;
    {
        AudioBuffer<8> buffer;
        buffer.push({0.25f, -0.25f});
        short pcm[] = {16000, 16000}; int flags = 0;
        mix(buffer, pcm, 1, 2, flags, 1, 1, false);
        check(pcm[0] == 8192 && pcm[1] == -8192, "closed native mic gate must not leak microphone");
        check(flags == 3, "desktop requests modified/send bits");
        pcm[0] = pcm[1] = 16000; flags = 0;
        mix(buffer, pcm, 1, 2, flags, 1, 1, false);
        check(pcm[0] == 0 && pcm[1] == 0 && flags == 1, "underflow produces silence when mic gate closed");
    }
    {
        AudioBuffer<8> buffer;
        buffer.push({0.8f, -0.8f});
        short pcm[] = {30000, -30000}; int flags = 2;
        mix(buffer, pcm, 1, 2, flags, 1, 1, false);
        check(pcm[0] == 32767 && pcm[1] == -32768, "overload saturates without signed wraparound");
        pcm[0] = pcm[1] = 1234; flags = 2;
        mix(buffer, pcm, 1, 2, flags, 0.5f, 1, false);
        check(pcm[0] == 1234 && pcm[1] == 1234 && flags == 3, "desktop underflow preserves allowed microphone");
    }
    {
        AudioBuffer<8> buffer;
        buffer.push({0.4f, 0.2f}); short pcm = 16000; int flags = 2;
        mix(buffer, &pcm, 1, 1, flags, 0.5f, 1, true);
        check(pcm == 4915 && flags == 3, "mono downmix and independent microphone mute");
        buffer.push({1, 1}); pcm = 1234; flags = 2;
        mix(buffer, &pcm, 1, 1, flags, 0, 0.5f, false);
        check(pcm == 617, "independent gains");
    }
    {
        AudioBuffer<8> buffer;
        for (int i = 0; i < 8; ++i) check(buffer.push({static_cast<float>(i), 0}), "capacity accepts frame");
        check(!buffer.push({99, 0}) && buffer.dropped() == 1, "overflow bounded");
        buffer.keepNewest(2);
        StereoFrame frame;
        check(buffer.pop(frame) && frame.left == 6 && buffer.trimmed() == 6, "latency recovery keeps newest frames");
        check(buffer.pop(frame) && frame.left == 7 && !buffer.pop(frame), "FIFO after trim");
        buffer.reset(); check(buffer.size() == 0 && buffer.dropped() == 0, "restart clears previous session");
    }
    {
        AudioBuffer<8> buffer;
        for (int i = 0; i < 100; ++i) buffer.push({0.9f, 0.9f});
        short pcm = 1234; int flags = 0;
        mix(buffer, &pcm, 1, 1, flags, 1, 1, false);
        check(pcm == 0 && buffer.size() == 0, "resuming after overflow never replays stale desktop audio");
        buffer.push({0.25f, 0.25f}); flags = 0;
        mix(buffer, &pcm, 1, 1, flags, 1, 1, false);
        check(pcm == 8192 && flags == 3, "new audio resumes after overflow recovery");
    }
    {
        AudioBuffer<> buffer;
        for (int i = 0; i < 6000; ++i) buffer.push({0.1f, 0.1f});
        short pcm = 0; int flags = 0;
        mix(buffer, &pcm, 1, 1, flags, 1, 1, false);
        check(buffer.size() == 1919 && buffer.trimmed() == 4080, "callback backlog recovers to bounded latency");
    }
    {
        AudioBuffer<128> buffer;
        constexpr int count = 200000;
        std::thread producer([&] {
            for (int i = 0; i < count; ++i)
                while (!buffer.push({static_cast<float>(i), -static_cast<float>(i)})) std::this_thread::yield();
        });
        for (int i = 0; i < count; ++i) {
            StereoFrame frame;
            while (!buffer.pop(frame)) std::this_thread::yield();
            check(frame.left == i && frame.right == -i, "concurrent producer/consumer ordering");
        }
        producer.join(); check(buffer.size() == 0, "concurrent ring drained");
    }
    {
        AudioBuffer<8> first, second;
        AudioBuffer<8>* sources[] = { &first, &second };
        first.push({0.25f, -0.25f}); second.push({0.125f, -0.125f});
        short pcm[] = {4096, 4096}; int flags = 2;
        mixSources(sources, 2, pcm, 1, 2, flags, 1, 1, false);
        check(pcm[0] == 16384 && pcm[1] == -8192, "multiple applications plus microphone exactly once");
        first.push({0.9f, -0.9f}); second.push({-0.9f, 0.9f});
        pcm[0] = pcm[1] = 2000; flags = 2;
        mixSources(sources, 2, pcm, 1, 2, flags, 2, 1, false);
        check(pcm[0] == 2000 && pcm[1] == 2000, "sum sources before saturation");
    }
    check(toPcm16(std::numeric_limits<float>::quiet_NaN()) == 0, "invalid sample becomes silence");
    for (const float gain : {0.0f, 0.5f, 1.0f, 2.0f, 3.0f}) {
        AudioBuffer<8> buffer; buffer.push({0.125f, -0.125f});
        short pcm[] = {0, 0}; int flags = 0;
        const auto result = mix(buffer, pcm, 1, 2, flags, gain, 0, false);
        check(pcm[0] == static_cast<short>(4096 * gain) && pcm[1] == static_cast<short>(-4096 * gain) &&
            result.peaks.desktop == 0.125f * gain, "shared post-gain level agrees with actual PCM across 0 to 300 percent");
    }
    {
        AudioBuffer<8> buffer;
        buffer.push({0.5f, -0.25f}); short pcm[] = {-16384, 8192}; int flags = 2;
        const auto result = mix(buffer, pcm, 1, 2, flags, 3, 2, false);
        check(result.peaks.desktop == 1.5f && result.peaks.microphone == 1.0f,
            "separate post-gain peaks retain overload before mixing/clipping");
        check(pcm[0] == 16384 && pcm[1] == -8192, "opposing signals do not hide individual source levels");
        buffer.push({0.25f, -0.25f}); short mono = 12345; flags = 0;
        const auto silent = mix(buffer, &mono, 1, 1, flags, 3, 3, false);
        check(silent.peaks.desktop == 0 && silent.peaks.microphone == 0, "meter follows actual mono downmix and native microphone gate");
        buffer.push({0.1f, 0.1f}); mono = 4096; flags = 2;
        const auto triple = mix(buffer, &mono, 1, 1, flags, 3, 3, false);
        check(std::abs(triple.peaks.desktop - 0.3f) < 0.00001f && triple.peaks.microphone == 0.375f && mono == 22118,
            "300 percent gains affect both meters and actual PCM");
        buffer.push({0.9f, 0.9f}); mono = 20000; flags = 2;
        const auto muted = mix(buffer, &mono, 1, 1, flags, 0, 3, true);
        check(muted.peaks.desktop == 0 && muted.peaks.microphone == 0 && mono == 0, "zero gain and independent mute silence corresponding meters");
        AudioBuffer<8> second; AudioBuffer<8>* sources[] = {&buffer, &second};
        buffer.push({0.5f, 0.5f}); second.push({-0.5f, -0.5f}); flags = 2;
        const auto cancelled = mixSources(sources, 2, &mono, 1, 1, flags, 3, 0, false);
        check(cancelled.peaks.desktop == 0, "shared meter measures sum of applications rather than loudest application");
        buffer.push({0.25f, 0.25f}); mono = 4096; flags = 2;
        const auto unmeasured = mix(buffer, &mono, 1, 1, flags, 1, 1, false, false);
        check(mono == 12288 && flags == 3 && unmeasured.peaks.desktop == 0 && unmeasured.peaks.microphone == 0,
            "disabled metering skips peak measurement without changing actual audio");
    }
    {
        PeakAccumulator accumulator;
        accumulator.publish({0.8f, 0.2f}); accumulator.publish({0.1f, 1.5f});
        const auto peaks = accumulator.take();
        check(peaks.desktop == 0.8f && peaks.microphone == 1.5f, "short peaks survive between GUI polls independently");
        const auto silent = accumulator.take();
        check(silent.desktop == 0 && silent.microphone == 0, "no callbacks cannot retain stale peaks");
        check(meterPosition(0) == 0 && meterPosition(0.001f) == 0 && meterPosition(0.1f) == 400 && meterPosition(3) == 600,
            "meters share logarithmic scale and saturate display at full scale");
        check(std::abs(peakDb(3) - 9.542425f) < 0.0001f, "overload readout preserves positive dBFS");
        MeterEnvelope envelope;
        const auto first = envelope.update(1, 0.033f), second = envelope.update(1, 0.033f);
        check(first > 0 && first < second && second < 1, "attack rises smoothly rather than jumping to each incoming peak");
        for (int i = 0; i < 50; ++i) envelope.update(1, 0.033f);
        check(envelope.update(1, 0.033f) > 0.999f, "steady signal settles to its post-gain peak");
        check(envelope.update(0, 0.033f) > 0.999f, "brief hold bridges adjacent refresh intervals");
        const auto falling = envelope.update(0, 0.3f);
        check(falling > 0.1f && falling < 0.9f, "release smoothly decays instead of snapping to silence");
        check(envelope.update(0, 5) == 0, "meter eventually returns to silence");
        envelope.update(2, 0.033f);
        check(envelope.overloaded(), "raw transient overload remains visible despite smoothed amplitude");
        envelope.update(0, 0.3f); check(envelope.overloaded(), "overload indicator holds briefly");
        envelope.update(0, 0.6f); check(!envelope.overloaded(), "overload indicator expires");
        envelope.reset();
        check(envelope.update(0, 0) == 0, "stopping resets display without a stale tail");
        const auto quiet = envelope.update(0.0001f, 0.033f);
        check(quiet > 0 && quiet < 0.0001f, "quiet nonzero input is not discarded below the bar scale");
        MeterEnvelope coarse, fine;
        const auto coarseValue = coarse.update(1, 0.1f);
        float fineValue = 0; for (int i = 0; i < 10; ++i) fineValue = fine.update(1, 0.01f);
        check(std::abs(coarseValue - fineValue) < 0.00001f, "smoothing is based on elapsed time rather than refresh count");
        MeterEnvelope gainChange;
        for (int i = 0; i < 50; ++i) gainChange.update(0.25f, 0.033f);
        gainChange.rescale(2);
        check(std::abs(gainChange.update(0.5f, 0) - 0.5f) < 0.0001f, "gain changes rescale the envelope immediately without replaying old gain");
        gainChange.rescale(0);
        check(gainChange.update(0, 0) == 0 && !gainChange.overloaded(), "zero gain immediately clears held level and overload");
    }
    if (failures) return 1;
    std::cout << "All audio core tests passed.\n"; return 0;
}

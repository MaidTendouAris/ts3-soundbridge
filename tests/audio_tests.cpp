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
    if (failures) return 1;
    std::cout << "All audio core tests passed.\n"; return 0;
}

#include "windows/loopback_capture.h"
#include "bridge_engine.h"
#include <filesystem>
#include <mmsystem.h>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <string>

namespace {
constexpr double pi = 3.14159265358979323846;
int tone(double frequency, double seconds) {
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    HWAVEOUT output = nullptr;
    auto result = waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) return static_cast<int>(result);
    std::vector<short> samples(static_cast<std::size_t>(48000 * seconds) * 2);
    for (std::size_t i = 0; i < samples.size() / 2; ++i)
        samples[i * 2] = samples[i * 2 + 1] = static_cast<short>(std::sin(2 * pi * frequency * i / 48000) * 983);
    WAVEHDR header{};
    header.lpData = reinterpret_cast<char*>(samples.data());
    header.dwBufferLength = static_cast<DWORD>(samples.size() * sizeof(short));
    result = waveOutPrepareHeader(output, &header, sizeof(header));
    if (result == MMSYSERR_NOERROR) result = waveOutWrite(output, &header, sizeof(header));
    if (result == MMSYSERR_NOERROR) while (!(header.dwFlags & WHDR_DONE)) Sleep(10);
    waveOutReset(output);
    if (header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(output, &header, sizeof(header));
    waveOutClose(output);
    return static_cast<int>(result);
}
int selectedProbe(double seconds, const std::vector<std::wstring>& paths) {
    soundbridge::Settings settings;
    settings.mode = soundbridge::CaptureMode::SelectedApplications;
    settings.applications = paths; settings.desktopPercent = 100; settings.microphonePercent = 0;
    soundbridge::BridgeEngine engine(settings, [](std::uint64_t, std::wstring&) { return true; });
    engine.start(1);
    for (int i = 0; i < 400 && engine.status().phase != soundbridge::BridgePhase::Sharing; ++i) {
        if (engine.status().phase == soundbridge::BridgePhase::Error) return 1;
        Sleep(20);
    }
    if (engine.status().phase != soundbridge::BridgePhase::Sharing) { std::cerr << "Selected apps did not start.\n"; return 1; }
    std::cout << "READY selected sources=" << engine.status().sources << '\n' << std::flush;
    // Short Hann windows keep detection independent of callback scheduling and
    // the initial phases of the independent process-loopback streams.
    constexpr std::size_t blockSize = 4800;
    const double frequencies[] = {440, 997, 1499};
    double real[3]{}, imaginary[3]{}, amplitudes[3]{}, weights = 0;
    std::size_t blockFrame = 0, blocks = 0;
    std::uint64_t frames = 0;
    std::vector<short> pcm(480 * 2);
    auto next = std::chrono::steady_clock::now();
    const auto deadline = next + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        std::fill(pcm.begin(), pcm.end(), short{0}); int flags = 0;
        engine.edit(1, pcm.data(), 480, 2, &flags);
        for (std::size_t i = 0; i < 480; ++i) {
            const double weight = 0.5 - 0.5 * std::cos(2 * pi * blockFrame / (blockSize - 1));
            const double sample = (static_cast<double>(pcm[i * 2]) + pcm[i * 2 + 1]) / 65536;
            for (int j = 0; j < 3; ++j) {
                const double phase = 2 * pi * frequencies[j] * blockFrame / 48000;
                real[j] += sample * weight * std::cos(phase); imaginary[j] += sample * weight * std::sin(phase);
            }
            weights += weight; ++blockFrame; ++frames;
            if (blockFrame == blockSize) {
                for (int j = 0; j < 3; ++j) { amplitudes[j] += 2 * std::hypot(real[j], imaginary[j]) / weights; real[j] = imaginary[j] = 0; }
                ++blocks; blockFrame = 0; weights = 0;
            }
        }
        next += std::chrono::milliseconds(10); std::this_thread::sleep_until(next);
    }
    const auto status = engine.status(); engine.stop();
    const double divisor = blocks ? static_cast<double>(blocks) : 1;
    std::cout << std::setprecision(9) << "frames=" << frames << " sources=" << status.sources
        << " amplitude440=" << amplitudes[0] / divisor << " amplitude997=" << amplitudes[1] / divisor
        << " amplitude1499=" << amplitudes[2] / divisor << " dropped=" << status.dropped << " trimmed=" << status.trimmed << '\n';
    return blocks && status.phase != soundbridge::BridgePhase::Error ? 0 : 1;
}
int watchSelected(double seconds, const std::wstring& path) {
    soundbridge::Settings settings; settings.mode = soundbridge::CaptureMode::SelectedApplications;
    settings.applications = {path}; settings.microphonePercent = 0;
    soundbridge::BridgeEngine engine(settings, [](std::uint64_t, std::wstring&) { return true; });
    engine.start(1);
    auto previous = soundbridge::BridgePhase::Stopped;
    std::size_t transitions = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto status = engine.status();
        if (status.phase == soundbridge::BridgePhase::Error) { std::cerr << "watch failed\n"; return 1; }
        if ((status.phase == soundbridge::BridgePhase::Waiting || status.phase == soundbridge::BridgePhase::Sharing) && status.phase != previous) {
            previous = status.phase; ++transitions;
            std::cout << "WATCH " << (previous == soundbridge::BridgePhase::Waiting ? "waiting" : "sharing") << '\n' << std::flush;
        }
        short pcm[960]{}; int flags = 0; engine.edit(1, pcm, 480, 2, &flags); Sleep(10);
    }
    engine.stop();
    std::cout << "transitions=" << transitions << '\n';
    return transitions >= 4 ? 0 : 1;
}
int probe(double seconds, DWORD excludedPid, double selfTone) {
    soundbridge::AudioBuffer<> buffer;
    soundbridge::LoopbackCapture capture(buffer);
    const auto hr = capture.start(excludedPid);
    if (FAILED(hr)) {
        std::cerr << "capture failed HRESULT=0x" << std::hex << static_cast<unsigned long>(hr) << '\n'; return 1;
    }
    std::cout << "READY pid=" << GetCurrentProcessId() << " excluded=" << excludedPid << " format=48000/stereo/PCM16\n" << std::flush;
    std::thread player;
    if (selfTone > 0) player = std::thread([=] { tone(selfTone, seconds); });
    double energy = 0, peak = 0, cos440 = 0, sin440 = 0, cos997 = 0, sin997 = 0;
    std::uint64_t count = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline && capture.running()) {
        soundbridge::StereoFrame frame;
        while (buffer.pop(frame)) {
            const double sample = (frame.left + frame.right) * 0.5;
            energy += sample * sample; peak = std::max(peak, std::abs(sample));
            const double time = static_cast<double>(count) / 48000;
            cos440 += sample * std::cos(2 * pi * 440 * time); sin440 += sample * std::sin(2 * pi * 440 * time);
            cos997 += sample * std::cos(2 * pi * 997 * time); sin997 += sample * std::sin(2 * pi * 997 * time);
            ++count;
        }
        Sleep(2);
    }
    capture.stop();
    if (player.joinable()) player.join();
    const double n = count ? static_cast<double>(count) : 1;
    std::cout << std::setprecision(9) << "frames=" << count << " rms=" << std::sqrt(energy / n)
              << " peak=" << peak << " amplitude440=" << 2 * std::hypot(cos440, sin440) / n
              << " amplitude997=" << 2 * std::hypot(cos997, sin997) / n
              << " dropped=" << buffer.dropped() << " discontinuities=" << capture.discontinuities()
              << " HRESULT=0x" << std::hex << static_cast<unsigned long>(capture.error()) << '\n';
    return FAILED(capture.error()) ? 1 : 0;
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "watch-selected") {
            const double seconds = std::stod(argv[2]);
            if (!std::isfinite(seconds) || seconds <= 0 || seconds > 60) return 2;
            return watchSelected(seconds, std::filesystem::path(argv[3]).wstring());
        }
        if (argc >= 4 && std::string(argv[1]) == "selected") {
            const double seconds = std::stod(argv[2]);
            if (!std::isfinite(seconds) || seconds <= 0 || seconds > 60) return 2;
            std::vector<std::wstring> paths;
            for (int i = 3; i < argc; ++i) paths.push_back(std::filesystem::path(argv[i]).wstring());
            return selectedProbe(seconds, paths);
        }
        if (argc >= 4 && std::string(argv[1]) == "tone") {
            const double frequency = std::stod(argv[2]), seconds = std::stod(argv[3]);
            if (!std::isfinite(frequency) || frequency < 20 || frequency > 20000 || !std::isfinite(seconds) || seconds <= 0 || seconds > 60) return 2;
            return tone(frequency, seconds);
        }
        if (argc >= 3 && std::string(argv[1]) == "probe") {
            const double seconds = std::stod(argv[2]);
            if (!std::isfinite(seconds) || seconds <= 0 || seconds > 3600) return 2;
            const DWORD pid = argc > 3 ? std::stoul(argv[3]) : GetCurrentProcessId();
            const double ownTone = argc > 4 ? std::stod(argv[4]) : 0;
            if (!std::isfinite(ownTone) || ownTone < 0 || ownTone > 20000) return 2;
            return probe(seconds, pid ? pid : GetCurrentProcessId(), ownTone);
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
    std::cout << "soundbridge_probe probe <seconds> [excluded-pid; 0=self] [self-tone-hz]\n"
                 "soundbridge_probe selected <seconds> <exe-path> [exe-path...]\n"
                 "soundbridge_probe watch-selected <seconds> <exe-path> (restart lifecycle test)\n"
                 "soundbridge_probe tone <hz> <seconds> (3% amplitude; no audio saved)\n";
    return 2;
}

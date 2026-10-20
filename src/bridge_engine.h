#pragma once
#include "settings.h"
#include "audio_levels.h"
#include "windows/applications.h"
#include "windows/loopback_capture.h"
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace soundbridge {
enum class BridgePhase { Stopped, Starting, Sharing, Waiting, Error };
struct BridgeStatus {
    BridgePhase phase = BridgePhase::Stopped;
    Message message = Text::Stopped;
    std::uint64_t server = 0, capturedFrames = 0, callbacks = 0, mixedFrames = 0, underruns = 0;
    std::uint64_t dropped = 0, trimmed = 0, discontinuities = 0;
    std::size_t sources = 0, queuedFrames = 0;
};
class BridgeEngine {
public:
    struct Levels { AudioPeaks peaks; bool active = false, microphoneMuted = false; };
    using Validator = std::function<bool(std::uint64_t, Message&)>;
    BridgeEngine(Settings settings, Validator validator);
    ~BridgeEngine();
    void apply(Settings settings);
    Settings settings() const;
    BridgeStatus status() const;
    Levels takeLevels() noexcept;
    void setMeteringVisible(bool visible) noexcept;
    void start(std::uint64_t server);
    void stop() noexcept;
    void stopFor(std::uint64_t server) noexcept;
    void serverChanged(std::uint64_t server) noexcept;
    void toggleMicrophone();
    std::uint64_t requestedServer() const noexcept { return requestedServer_.load(); }
    void edit(std::uint64_t server, short* samples, int frames, int channels, int* flags) noexcept;
private:
    struct Source;
    void run() noexcept;
    void setStatus(BridgePhase phase, const Message& message);
    void clearSources();
    bool reconcile(const std::vector<ProcessInfo>& roots, bool include, std::uint64_t revision, Message& error);
    void updateStats();
    Validator validator_;
    mutable std::mutex settingsMutex_, statusMutex_;
    std::mutex audioMutex_, wakeMutex_;
    std::condition_variable wake_;
    Settings settings_;
    BridgeStatus status_;
    std::vector<std::unique_ptr<Source>> sources_;
    std::thread worker_;
    std::atomic<bool> closing_{false}, micMuted_{false};
    std::atomic<std::uint64_t> revision_{0}, requestedServer_{0}, activeServer_{0}, activeRevision_{0};
    std::atomic<float> desktopGain_{0.5f}, microphoneGain_{1.0f};
    PeakAccumulator peaks_;
    std::atomic<bool> metersEnabled_{false}, meterConsumer_{false};
    std::atomic<std::uint64_t> callbacks_{0}, mixedFrames_{0}, underruns_{0};
};
}

#include "bridge_engine.h"
#include "mixer.h"
#include <algorithm>
#include <chrono>
#include <sstream>

namespace soundbridge {
struct BridgeEngine::Source {
    ProcessInfo process;
    HANDLE processHandle = nullptr;
    bool include = false;
    AudioBuffer<> buffer;
    LoopbackCapture capture{buffer};
    ~Source() { capture.stop(); if (processHandle) CloseHandle(processHandle); }
};
BridgeEngine::BridgeEngine(Settings settings, Validator validator) : validator_(std::move(validator)) {
    apply(std::move(settings));
    worker_ = std::thread(&BridgeEngine::run, this);
}
BridgeEngine::~BridgeEngine() {
    closing_.store(true); activeServer_.store(0); requestedServer_.store(0);
    revision_.fetch_add(1); wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}
void BridgeEngine::apply(Settings settings) {
    settings.desktopPercent = std::clamp(settings.desktopPercent, 0, MaximumGainPercent);
    settings.microphonePercent = std::clamp(settings.microphonePercent, 0, MaximumGainPercent);
    bool routesChanged;
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        routesChanged = settings.mode != settings_.mode || settings.applications != settings_.applications;
        settings_ = std::move(settings);
        desktopGain_.store(settings_.desktopPercent / 100.0f);
        microphoneGain_.store(settings_.microphonePercent / 100.0f);
        micMuted_.store(settings_.microphoneMuted);
        metersEnabled_.store(settings_.showLevelMeters);
    }
    if (routesChanged) {
        activeServer_.store(0); revision_.fetch_add(1); wake_.notify_all();
    }
}
Settings BridgeEngine::settings() const { std::lock_guard<std::mutex> lock(settingsMutex_); return settings_; }
BridgeStatus BridgeEngine::status() const { std::lock_guard<std::mutex> lock(statusMutex_); return status_; }
BridgeEngine::Levels BridgeEngine::takeLevels() noexcept {
    Levels result;
    result.peaks = peaks_.take();
    const auto server = requestedServer_.load();
    result.active = server && activeServer_.load() == server && activeRevision_.load() == revision_.load() &&
        metersEnabled_.load() && meterConsumer_.load();
    result.microphoneMuted = micMuted_.load();
    if (!result.active) result.peaks = {};
    if (result.microphoneMuted) result.peaks.microphone = 0;
    return result;
}
void BridgeEngine::setMeteringVisible(bool visible) noexcept {
    meterConsumer_.store(visible); peaks_.reset();
}
void BridgeEngine::start(std::uint64_t server) {
    activeServer_.store(0); requestedServer_.store(server);
    callbacks_.store(0); mixedFrames_.store(0); underruns_.store(0);
    // A zero server still generates a validation error visible in the GUI.
    revision_.fetch_add(1); wake_.notify_all();
    if (!server) setStatus(BridgePhase::Error, L"请先连接 TS3 服务器，再开始共享。");
}
void BridgeEngine::stop() noexcept {
    activeServer_.store(0); requestedServer_.store(0); revision_.fetch_add(1); wake_.notify_all();
    setStatus(BridgePhase::Stopped, L"共享已关闭。");
}
void BridgeEngine::stopFor(std::uint64_t server) noexcept {
    auto expected = server;
    if (server && requestedServer_.compare_exchange_strong(expected, 0)) {
        activeServer_.store(0); revision_.fetch_add(1); wake_.notify_all();
    }
}
void BridgeEngine::serverChanged(std::uint64_t server) noexcept {
    const auto requested = requestedServer_.load();
    if (requested && requested != server) stopFor(requested);
}
void BridgeEngine::toggleMicrophone() {
    auto current = settings(); current.microphoneMuted = !current.microphoneMuted; apply(std::move(current));
}
void BridgeEngine::setStatus(BridgePhase phase, const std::wstring& message) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_.phase = phase; status_.message = message;
    status_.server = requestedServer_.load();
}
void BridgeEngine::clearSources() {
    activeServer_.store(0);
    std::vector<std::unique_ptr<Source>> old;
    { std::lock_guard<std::mutex> lock(audioMutex_); old.swap(sources_); peaks_.reset(); }
    // Joining capture threads takes place outside the audio-callback mutex.
    old.clear();
}
bool BridgeEngine::reconcile(const std::vector<ProcessInfo>& roots, bool include, std::uint64_t revision, std::wstring& error) {
    std::vector<std::unique_ptr<Source>> removed;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        for (auto it = sources_.begin(); it != sources_.end();) {
            const auto& source = **it;
            const bool wanted = source.include == include && source.capture.running() &&
                std::any_of(roots.begin(), roots.end(), [&](const auto& root) {
                    return root.pid == source.process.pid && root.created == source.process.created;
                });
            if (!wanted) { removed.push_back(std::move(*it)); it = sources_.erase(it); } else ++it;
        }
    }
    removed.clear();
    for (const auto& root : roots) {
        if (closing_.load() || revision_.load() != revision) return false;
        bool present;
        { std::lock_guard<std::mutex> lock(audioMutex_);
          present = std::any_of(sources_.begin(), sources_.end(), [&](const auto& source) { return source->process.pid == root.pid && source->process.created == root.created; }); }
        if (present) continue;
        auto source = std::make_unique<Source>(); source->process = root; source->include = include;
        source->processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, root.pid);
        ProcessInfo verified;
        if (!source->processHandle || !queryProcess(root.pid, verified) || verified.created != root.created ||
            !samePath(verified.path, root.path) || WaitForSingleObject(source->processHandle, 0) != WAIT_TIMEOUT) continue;
        const HRESULT result = source->capture.start(root.pid, include);
        if (FAILED(result)) {
            std::wostringstream text;
            text << L"无法采集 " << root.name << L"（0x" << std::hex << static_cast<unsigned long>(result) << L"）。请检查音频设备后重新开始。";
            error = text.str(); return false;
        }
        if (closing_.load() || revision_.load() != revision) return false;
        std::lock_guard<std::mutex> lock(audioMutex_); sources_.push_back(std::move(source));
    }
    return true;
}
void BridgeEngine::updateStats() {
    BridgeStatus statistics;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        statistics.sources = sources_.size();
        for (const auto& source : sources_) {
            statistics.capturedFrames += source->capture.frames();
            statistics.queuedFrames += source->buffer.size(); statistics.dropped += source->buffer.dropped();
            statistics.trimmed += source->buffer.trimmed(); statistics.discontinuities += source->capture.discontinuities();
        }
    }
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_.sources = statistics.sources; status_.capturedFrames = statistics.capturedFrames;
    status_.queuedFrames = statistics.queuedFrames; status_.dropped = statistics.dropped;
    status_.trimmed = statistics.trimmed; status_.discontinuities = statistics.discontinuities;
    status_.callbacks = callbacks_.load(); status_.mixedFrames = mixedFrames_.load(); status_.underruns = underruns_.load();
    status_.server = requestedServer_.load();
}
void BridgeEngine::run() noexcept {
    try {
        auto seenRevision = revision_.load();
        while (!closing_.load()) {
            const auto revision = revision_.load();
            const auto server = requestedServer_.load();
            const auto current = settings();
            if (!server) {
                clearSources();
                // Retain actionable start errors until the user changes settings/start.
                if (status().phase != BridgePhase::Error) setStatus(BridgePhase::Stopped, L"共享已关闭。");
            } else {
                std::wstring error;
                if (!validator_(server, error)) {
                    stopFor(server); clearSources(); setStatus(BridgePhase::Error, error);
                } else if (current.mode == CaptureMode::SelectedApplications && current.applications.empty()) {
                    stopFor(server); clearSources(); setStatus(BridgePhase::Error, L"请至少勾选一个应用。");
                } else {
                    if (revision != seenRevision) {
                        clearSources(); setStatus(BridgePhase::Starting, L"正在准备音频共享…");
                    }
                    std::vector<ProcessInfo> roots;
                    const bool include = current.mode == CaptureMode::SelectedApplications;
                    if (include) roots = selectRoots(processSnapshot(current.applications), current.applications, GetCurrentProcessId(), error);
                    else { ProcessInfo self; if (queryProcess(GetCurrentProcessId(), self)) roots.push_back(std::move(self)); }
                    if (!error.empty() || !reconcile(roots, include, revision, error)) {
                        if (revision_.load() == revision && !closing_.load()) {
                            stopFor(server); clearSources(); setStatus(BridgePhase::Error, error.empty() ? L"无法建立音频采集。" : error);
                        }
                    } else if (revision_.load() == revision && requestedServer_.load() == server) {
                        // Recheck native mute/connection after potentially slow activation.
                        if (!validator_(server, error)) { stopFor(server); clearSources(); setStatus(BridgePhase::Error, error); }
                        else {
                            activeRevision_.store(revision); activeServer_.store(server);
                            std::size_t sourceCount;
                            { std::lock_guard<std::mutex> lock(audioMutex_); sourceCount = sources_.size(); }
                            setStatus(sourceCount ? BridgePhase::Sharing : BridgePhase::Waiting,
                                sourceCount ? L"共享中 · 麦克风遵循 TS3 原有发言规则。" : L"等待所选应用运行；应用启动后会自动开始采集。" );
                        }
                    }
                }
            }
            seenRevision = revision;
            updateStats();
            std::unique_lock<std::mutex> lock(wakeMutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(750), [&] { return closing_.load() || revision_.load() != seenRevision; });
        }
    } catch (...) {
        activeServer_.store(0); requestedServer_.store(0);
        setStatus(BridgePhase::Error, L"音频操作失败，共享已停止。请重新加载插件。");
    }
    clearSources();
}
void BridgeEngine::edit(std::uint64_t server, short* samples, int frames, int channels, int* flags) noexcept {
    if (!flags || !samples || !server || requestedServer_.load() != server || activeRevision_.load() != revision_.load() || activeServer_.load() != server) return;
    std::unique_lock<std::mutex> lock(audioMutex_, std::try_to_lock);
    if (!lock.owns_lock() || requestedServer_.load() != server || activeRevision_.load() != revision_.load() || activeServer_.load() != server) return;
    AudioBuffer<>* buffers[16]; std::size_t count = 0;
    for (const auto& source : sources_) if (source->capture.running() && count < 16) buffers[count++] = &source->buffer;
    const bool measureLevels = metersEnabled_.load(std::memory_order_relaxed) && meterConsumer_.load(std::memory_order_relaxed);
    const auto result = mixSources(buffers, count, samples, frames, channels, *flags,
        desktopGain_.load(), microphoneGain_.load(), micMuted_.load(), measureLevels);
    if (measureLevels) peaks_.publish(result.peaks);
    callbacks_.fetch_add(1, std::memory_order_relaxed); mixedFrames_.fetch_add(result.desktopFrames, std::memory_order_relaxed);
    underruns_.fetch_add(result.underrunFrames, std::memory_order_relaxed);
}
}

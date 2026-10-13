#include "loopback_capture.h"
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <avrt.h>
#include <wrl.h>

namespace soundbridge {
using Microsoft::WRL::ComPtr;
namespace {
void moduleAddress() {}
class Activation final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
public:
    Activation() {
        event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        // Windows activation has no cancellation API. A timeout can leave a COM
        // callback outstanding after TS3 disables the plugin. Pin the prototype
        // module until process exit so that every late completion/Release stays
        // executable. Audio threads, devices and events still stop normally.
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&moduleAddress), &module_);
    }
    ~Activation() {
        if (event) CloseHandle(event);
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        ComPtr<IUnknown> unknown;
        HRESULT activationResult = E_UNEXPECTED;
        result = operation->GetActivateResult(&activationResult, &unknown);
        if (SUCCEEDED(result)) result = activationResult;
        if (SUCCEEDED(result)) result = unknown.As(&client);
        SetEvent(event);
        return S_OK;
    }
    HANDLE event = nullptr;
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;
    AUDIOCLIENT_ACTIVATION_PARAMS parameters{};
    PROPVARIANT variant{};
private:
    HMODULE module_ = nullptr;
};
struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};
struct MmcssScope {
    DWORD task = 0;
    HANDLE handle = AvSetMmThreadCharacteristicsW(L"Audio", &task);
    ~MmcssScope() { if (handle) AvRevertMmThreadCharacteristics(handle); }
};
}

HRESULT LoopbackCapture::start(DWORD targetPid, bool includeProcessTree) {
    stop();
    error_.store(E_PENDING); frames_.store(0); discontinuities_.store(0);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_ || !readyEvent_) {
        const auto hr = HRESULT_FROM_WIN32(GetLastError());
        error_.store(hr); stop(); return hr;
    }
    try { thread_ = std::thread(&LoopbackCapture::run, this, targetPid, includeProcessTree); }
    catch (...) { error_.store(E_OUTOFMEMORY); stop(); return E_OUTOFMEMORY; }
    // Activation itself has a five-second timeout; initialization may take longer.
    if (WaitForSingleObject(readyEvent_, 8000) != WAIT_OBJECT_0) {
        error_.store(HRESULT_FROM_WIN32(ERROR_TIMEOUT)); stop();
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    return error_.load();
}
void LoopbackCapture::stop() noexcept {
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    running_.store(false);
    if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
    if (readyEvent_) { CloseHandle(readyEvent_); readyEvent_ = nullptr; }
}
void LoopbackCapture::run(DWORD targetPid, bool includeProcessTree) noexcept {
    const auto finish = [this](HRESULT result) {
        error_.store(result); running_.store(false); SetEvent(readyEvent_);
    };
    ComScope com;
    if (FAILED(com.result)) { finish(com.result); return; }
    auto activation = Microsoft::WRL::Make<Activation>();
    if (!activation || !activation->event) { finish(E_OUTOFMEMORY); return; }
    activation->parameters.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activation->parameters.ProcessLoopbackParams.TargetProcessId = targetPid;
    activation->parameters.ProcessLoopbackParams.ProcessLoopbackMode = includeProcessTree ?
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
    activation->variant.vt = VT_BLOB;
    activation->variant.blob.cbSize = sizeof(activation->parameters);
    activation->variant.blob.pBlobData = reinterpret_cast<BYTE*>(&activation->parameters);
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient), &activation->variant, activation.Get(), &operation);
    if (FAILED(hr)) { finish(hr); return; }
    HANDLE activationEvents[] = { stopEvent_, activation->event };
    const auto waited = WaitForMultipleObjects(2, activationEvents, FALSE, 5000);
    if (waited != WAIT_OBJECT_0 + 1) {
        finish(waited == WAIT_OBJECT_0 ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        return;
    }
    hr = activation->result;
    if (FAILED(hr)) { finish(hr); return; }
    auto client = activation->client;
    // Windows converts the process mix into the TS3 callback's 48 kHz PCM.
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 192000;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        0, 0, &format, nullptr);
    if (FAILED(hr)) { finish(hr); return; }
    Handle samplesReady{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    if (!samplesReady.value) { finish(HRESULT_FROM_WIN32(GetLastError())); return; }
    hr = client->SetEventHandle(samplesReady.value);
    ComPtr<IAudioCaptureClient> capture;
    if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));
    if (SUCCEEDED(hr)) hr = client->Start();
    if (FAILED(hr)) { finish(hr); return; }
    MmcssScope priority;
    error_.store(S_OK); running_.store(true); SetEvent(readyEvent_);
    HANDLE events[] = { stopEvent_, samplesReady.value };
    while (true) {
        const auto wait = WaitForMultipleObjects(2, events, FALSE, 1000);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) { hr = HRESULT_FROM_WIN32(GetLastError()); break; }
        UINT32 packetFrames = 0;
        hr = capture->GetNextPacketSize(&packetFrames);
        if (FAILED(hr)) break;
        while (packetFrames) {
            BYTE* bytes = nullptr;
            UINT32 count = 0; DWORD flags = 0;
            hr = capture->GetBuffer(&bytes, &count, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) discontinuities_.fetch_add(1);
            const auto* pcm = reinterpret_cast<const short*>(bytes);
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !pcm;
            for (UINT32 i = 0; i < count; ++i) {
                const StereoFrame frame = silent ? StereoFrame{} : StereoFrame{pcm[i * 2] / 32768.0f, pcm[i * 2 + 1] / 32768.0f};
                buffer_.push(frame);
            }
            frames_.fetch_add(count);
            hr = capture->ReleaseBuffer(count);
            if (FAILED(hr) || WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) break;
            hr = capture->GetNextPacketSize(&packetFrames);
            if (FAILED(hr)) break;
        }
        if (FAILED(hr)) break;
    }
    client->Stop();
    finish(hr);
}
}

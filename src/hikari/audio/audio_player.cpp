#include "hikari/audio/audio_player.hpp"

#include <windows.h>

#include <mmreg.h>
#include <objbase.h>
#include <xaudio2.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace hikari::audio {
namespace {

std::string hr_error(HRESULT result) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%08lX",
                  static_cast<unsigned long>(result));
    return buffer;
}

} // namespace

struct AudioPlayer::Impl {
    IXAudio2* xaudio2 = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* source = nullptr;
    AudioTrack track;
    float volume = 1.0f;
    bool running = false;
    bool com_initialized = false;
    double base_seconds = 0.0;
    std::chrono::steady_clock::time_point started_at{};

    ~Impl() { shutdown(); }

    void shutdown() {
        if (source != nullptr) {
            source->Stop(0);
            source->FlushSourceBuffers();
        }
        running = false;
        if (source != nullptr) {
            source->DestroyVoice();
            source = nullptr;
        }
        if (master != nullptr) {
            master->DestroyVoice();
            master = nullptr;
        }
        if (xaudio2 != nullptr) {
            xaudio2->Release();
            xaudio2 = nullptr;
        }
        if (com_initialized) {
            CoUninitialize();
            com_initialized = false;
        }
    }

    bool ensure_source(std::string* error) {
        if (source != nullptr) {
            return true;
        }
        if (xaudio2 == nullptr) {
            if (error != nullptr) {
                *error = "audio engine is not initialized";
            }
            return false;
        }
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = static_cast<WORD>(kOutputChannels);
        format.nSamplesPerSec = static_cast<DWORD>(kOutputSampleRate);
        format.wBitsPerSample = 32;
        format.nBlockAlign = static_cast<WORD>(kOutputChannels * sizeof(float));
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        const HRESULT result = xaudio2->CreateSourceVoice(&source, &format);
        if (FAILED(result)) {
            if (error != nullptr) {
                *error = "cannot create audio voice: " + hr_error(result);
            }
            source = nullptr;
            return false;
        }
        source->SetVolume(volume);
        return true;
    }
};

AudioPlayer::AudioPlayer() : impl_(std::make_unique<Impl>()) {}

AudioPlayer::~AudioPlayer() = default;

bool AudioPlayer::initialize(std::string* error) {
    if (impl_->xaudio2 != nullptr) {
        return true;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(com)) {
        impl_->com_initialized = true;
    } else if (com != RPC_E_CHANGED_MODE) {
        if (error != nullptr) {
            *error = "cannot initialize COM for audio: " + hr_error(com);
        }
        return false;
    }
    const HRESULT created = XAudio2Create(&impl_->xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(created)) {
        if (error != nullptr) {
            *error = "cannot create XAudio2 engine: " + hr_error(created);
        }
        impl_->shutdown();
        return false;
    }
    const HRESULT mastering = impl_->xaudio2->CreateMasteringVoice(&impl_->master);
    if (FAILED(mastering)) {
        if (error != nullptr) {
            *error = "cannot create audio output: " + hr_error(mastering);
        }
        impl_->shutdown();
        return false;
    }
    return true;
}

void AudioPlayer::load(AudioTrack track, std::string* error) {
    stop();
    if (impl_->source != nullptr) {
        impl_->source->DestroyVoice();
        impl_->source = nullptr;
    }
    impl_->track = std::move(track);
    impl_->base_seconds = 0.0;
    impl_->running = false;
    if (impl_->track.samples.empty()) {
        return;
    }
    if (impl_->xaudio2 == nullptr && !initialize(error)) {
        impl_->track = AudioTrack{};
        return;
    }
    if (!impl_->ensure_source(error)) {
        impl_->track = AudioTrack{};
    }
}

void AudioPlayer::unload() {
    stop();
    if (impl_->source != nullptr) {
        impl_->source->DestroyVoice();
        impl_->source = nullptr;
    }
    impl_->track = AudioTrack{};
    impl_->base_seconds = 0.0;
    impl_->running = false;
}

bool AudioPlayer::has_track() const { return !impl_->track.samples.empty(); }

void AudioPlayer::play(double seconds) {
    if (!has_track() || !impl_->ensure_source(nullptr)) {
        return;
    }
    impl_->source->Stop(0);
    impl_->source->FlushSourceBuffers();
    impl_->running = false;

    const double clamped =
        std::clamp(seconds, 0.0, impl_->track.duration_seconds);
    const auto offset = static_cast<std::int64_t>(clamped * kOutputSampleRate);
    if (offset >= impl_->track.frame_count) {
        impl_->base_seconds = impl_->track.duration_seconds;
        return;
    }

    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(
        (impl_->track.frame_count - offset) * kOutputChannels * sizeof(float));
    buffer.pAudioData = reinterpret_cast<const BYTE*>(
        impl_->track.samples.data() + offset * kOutputChannels);
    if (FAILED(impl_->source->SubmitSourceBuffer(&buffer))) {
        return;
    }
    if (FAILED(impl_->source->Start(0))) {
        return;
    }
    impl_->base_seconds = static_cast<double>(offset) / kOutputSampleRate;
    impl_->started_at = std::chrono::steady_clock::now();
    impl_->running = true;
}

void AudioPlayer::stop() {
    if (impl_->running) {
        impl_->base_seconds = position_seconds();
        impl_->running = false;
    }
    if (impl_->source != nullptr) {
        impl_->source->Stop(0);
        impl_->source->FlushSourceBuffers();
    }
}

void AudioPlayer::set_volume(float volume) {
    impl_->volume = std::clamp(volume, 0.0f, 1.0f);
    if (impl_->source != nullptr) {
        impl_->source->SetVolume(impl_->volume);
    }
}

bool AudioPlayer::playing() const { return impl_->running; }

double AudioPlayer::position_seconds() const {
    if (!impl_->running) {
        return impl_->base_seconds;
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - impl_->started_at);
    return std::min(impl_->base_seconds + elapsed.count(),
                    impl_->track.duration_seconds);
}

double AudioPlayer::duration_seconds() const { return impl_->track.duration_seconds; }

} // namespace hikari::audio

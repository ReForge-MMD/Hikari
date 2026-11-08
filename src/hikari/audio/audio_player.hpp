#pragma once

#include <memory>
#include <string>

#include "hikari/audio/audio_track.hpp"

namespace hikari::audio {

// XAudio2-backed preview player. Decoding is pure FFmpeg; playback keeps a
// contiguous float buffer and simply restarts the voice when the timeline is
// seeked or drifts away from the frame clock.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool initialize(std::string* error);
    void load(AudioTrack track, std::string* error);
    void unload();
    bool has_track() const;

    void play(double seconds);
    void stop();
    void set_volume(float volume);

    bool playing() const;
    double position_seconds() const;
    double duration_seconds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hikari::audio

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hikari::audio {

// Everything is decoded to one fixed output format so the player and the
// waveform never have to care about the source container or codec.
inline constexpr int kOutputSampleRate = 48000;
inline constexpr int kOutputChannels = 2;
inline constexpr int kPeakBlockFrames = 512;

struct AudioTrack {
    std::string path;
    std::int64_t frame_count = 0;
    double duration_seconds = 0.0;
    // Interleaved stereo float samples at kOutputSampleRate.
    std::vector<float> samples;
    // Maximum absolute amplitude per kPeakBlockFrames frames, for the timeline.
    std::vector<float> peaks;
};

struct DecodeResult {
    AudioTrack track;
    std::string error;

    bool ok() const { return error.empty(); }
};

// Decodes any container/codec FFmpeg was built with. The path is UTF-8.
DecodeResult decode_audio_file(const std::string& utf8_path);

} // namespace hikari::audio

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hikari::video {

// Containers exposed by the export dialog. Video streams use the Media
// Foundation encoders where the platform provides them; the legacy AVI path
// falls back to the built-in MPEG-4/MP3 pair so old players keep working.
enum class VideoFormat {
    Mp4H264,
    Mp4Hevc,
    MovH264,
    MkvH264,
    MpegTsH264,
    AviMpeg4,
};

struct VideoFormatInfo {
    VideoFormat format;
    const char* label;
    const char* extension;
    const char* muxer;
    const char* video_encoder;
    const char* audio_encoder;
};

const std::vector<VideoFormatInfo>& video_formats();
const VideoFormatInfo& video_format_info(VideoFormat format);

// True when the FFmpeg build exposes both the video and audio encoders.
bool video_format_available(VideoFormat format);

struct VideoEncoderSettings {
    VideoFormat format = VideoFormat::Mp4H264;
    int width = 1280;
    int height = 720;
    int fps = 30;
    int video_bitrate = 8'000'000;
    int audio_bitrate = 192'000;
    bool include_audio = true;
};

// Encodes top-down RGBA frames and interleaved stereo float samples into one
// file. The encoder owns its FFmpeg contexts and is not thread safe.
class VideoEncoder {
public:
    explicit VideoEncoder(VideoEncoderSettings settings);
    ~VideoEncoder();

    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    bool open(const std::string& utf8_path, std::string* error);
    bool write_video_frame(const std::uint8_t* rgba, std::string* error);
    bool write_audio(const float* interleaved_stereo, std::size_t sample_frames,
                     std::string* error);
    bool finish(std::string* error);
    bool has_audio() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Runs the encoder on a dedicated multithreaded-apartment thread. Media
// Foundation encoders reject callers that already initialized COM as STA,
// which is the case for the Qt GUI thread.
class VideoEncoderThread {
public:
    VideoEncoderThread();
    ~VideoEncoderThread();

    VideoEncoderThread(const VideoEncoderThread&) = delete;
    VideoEncoderThread& operator=(const VideoEncoderThread&) = delete;

    bool open(const std::string& utf8_path, const VideoEncoderSettings& settings,
              std::string* error);
    bool write_video_frame(const std::uint8_t* rgba, std::string* error);
    bool write_audio(const float* interleaved_stereo, std::size_t sample_frames,
                     std::string* error);
    bool finish(std::string* error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Exposed for unit tests. Both convert top-down RGBA into limited-range
// BT.709 chroma; the output is padded by replicating the last source row and
// column when the requested size is larger than the source.
void rgba_to_yuv420p(const std::uint8_t* rgba, int source_width, int source_height,
                     std::uint8_t* y, int y_stride,
                     std::uint8_t* u, int u_stride,
                     std::uint8_t* v, int v_stride,
                     int output_width, int output_height);

void rgba_to_nv12(const std::uint8_t* rgba, int source_width, int source_height,
                  std::uint8_t* y, int y_stride,
                  std::uint8_t* uv, int uv_stride,
                  int output_width, int output_height);

} // namespace hikari::video

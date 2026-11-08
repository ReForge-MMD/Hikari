#include "hikari/video/video_encoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

namespace hikari::video {
namespace {

std::string ff_error(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

bool set_error(const std::string& message, std::string* error) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

struct Rgba8 {
    int r = 0;
    int g = 0;
    int b = 0;
};

Rgba8 sample_rgba(const std::uint8_t* rgba, int source_width, int source_height, int x, int y) {
    const int sx = std::min(x, source_width - 1);
    const int sy = std::min(y, source_height - 1);
    const std::size_t offset =
        (static_cast<std::size_t>(sy) * static_cast<std::size_t>(source_width) +
         static_cast<std::size_t>(sx)) *
        4;
    const std::uint8_t* pixel = rgba + offset;
    return {pixel[0], pixel[1], pixel[2]};
}

int clamp_int(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

void convert_rgba(const std::uint8_t* rgba, int source_width, int source_height,
                  int output_width, int output_height,
                  std::uint8_t* y_plane, int y_stride,
                  std::uint8_t* u_plane, int u_stride,
                  std::uint8_t* v_plane, int v_stride, bool interleaved_uv) {
    for (int y = 0; y < output_height; ++y) {
        std::uint8_t* row = y_plane + static_cast<std::ptrdiff_t>(y) * y_stride;
        for (int x = 0; x < output_width; ++x) {
            const Rgba8 c = sample_rgba(rgba, source_width, source_height, x, y);
            const int luma = (187 * c.r + 629 * c.g + 63 * c.b + 512) >> 10;
            row[x] = static_cast<std::uint8_t>(clamp_int(16 + luma, 16, 235));
        }
    }

    for (int y = 0; y < output_height / 2; ++y) {
        const auto row_offset = static_cast<std::ptrdiff_t>(y) * u_stride;
        std::uint8_t* u_row = interleaved_uv ? nullptr : u_plane + row_offset;
        std::uint8_t* v_row =
            interleaved_uv ? nullptr : v_plane + static_cast<std::ptrdiff_t>(y) * v_stride;
        std::uint8_t* uv_row = interleaved_uv ? u_plane + row_offset : nullptr;
        for (int x = 0; x < output_width / 2; ++x) {
            int r = 0;
            int g = 0;
            int b = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const Rgba8 c = sample_rgba(rgba, source_width, source_height,
                                                x * 2 + dx, y * 2 + dy);
                    r += c.r;
                    g += c.g;
                    b += c.b;
                }
            }
            const float rf = static_cast<float>(r) * 0.25f;
            const float gf = static_cast<float>(g) * 0.25f;
            const float bf = static_cast<float>(b) * 0.25f;
            const float luma = 0.2126f * rf + 0.7152f * gf + 0.0722f * bf;
            const int u = clamp_int(
                128 + static_cast<int>(std::lround((bf - luma) * (224.0f / 255.0f) / 1.8556f)),
                16, 240);
            const int v = clamp_int(
                128 + static_cast<int>(std::lround((rf - luma) * (224.0f / 255.0f) / 1.5748f)),
                16, 240);
            if (interleaved_uv) {
                uv_row[x * 2 + 0] = static_cast<std::uint8_t>(u);
                uv_row[x * 2 + 1] = static_cast<std::uint8_t>(v);
            } else {
                u_row[x] = static_cast<std::uint8_t>(u);
                v_row[x] = static_cast<std::uint8_t>(v);
            }
        }
    }
}

} // namespace

const std::vector<VideoFormatInfo>& video_formats() {
    static const std::vector<VideoFormatInfo> formats = {
        {VideoFormat::Mp4H264, "MP4 (H.264 + AAC)", "mp4", "mp4", "h264_mf", "aac_mf"},
        {VideoFormat::Mp4Hevc, "MP4 (HEVC + AAC)", "mp4", "mp4", "hevc_mf", "aac_mf"},
        {VideoFormat::MovH264, "MOV (H.264 + AAC)", "mov", "mov", "h264_mf", "aac_mf"},
        {VideoFormat::MkvH264, "MKV (H.264 + AAC)", "mkv", "matroska", "h264_mf", "aac_mf"},
        {VideoFormat::MpegTsH264, "TS (H.264 + AAC)", "ts", "mpegts", "h264_mf", "aac_mf"},
        {VideoFormat::AviMpeg4, "AVI (MPEG-4 + PCM)", "avi", "avi", "mpeg4", "pcm_s16le"},
    };
    return formats;
}

const VideoFormatInfo& video_format_info(VideoFormat format) {
    for (const VideoFormatInfo& info : video_formats()) {
        if (info.format == format) {
            return info;
        }
    }
    return video_formats().front();
}

namespace {

bool probe_video_format(VideoFormat format) {
    const VideoFormatInfo& info = video_format_info(format);
    const AVCodec* video_codec = avcodec_find_encoder_by_name(info.video_encoder);
    if (video_codec == nullptr) {
        return false;
    }
    if (info.audio_encoder != nullptr &&
        avcodec_find_encoder_by_name(info.audio_encoder) == nullptr) {
        return false;
    }
    AVCodecContext* probe = avcodec_alloc_context3(video_codec);
    if (probe == nullptr) {
        return false;
    }
    probe->width = 64;
    probe->height = 64;
    probe->pix_fmt = std::strcmp(info.video_encoder, "mpeg4") == 0 ? AV_PIX_FMT_YUV420P
                                                                   : AV_PIX_FMT_NV12;
    probe->time_base = AVRational{1, 30};
    probe->framerate = AVRational{30, 1};
    probe->bit_rate = 1'000'000;
    if (std::strcmp(info.video_encoder, "h264_mf") == 0) {
        probe->profile = AV_PROFILE_H264_MAIN;
    } else if (std::strcmp(info.video_encoder, "hevc_mf") == 0) {
        probe->profile = AV_PROFILE_HEVC_MAIN;
    }
    const int result = avcodec_open2(probe, video_codec, nullptr);
    avcodec_free_context(&probe);
    return result >= 0;
}

} // namespace

bool video_format_available(VideoFormat format) {
    // Media Foundation encoders reject STA callers, so probe from a worker
    // thread that lets FFmpeg initialize the multithreaded apartment.
    bool available = false;
    std::thread probe([format, &available] { available = probe_video_format(format); });
    probe.join();
    return available;
}

void rgba_to_yuv420p(const std::uint8_t* rgba, int source_width, int source_height,
                     std::uint8_t* y, int y_stride,
                     std::uint8_t* u, int u_stride,
                     std::uint8_t* v, int v_stride,
                     int output_width, int output_height) {
    convert_rgba(rgba, source_width, source_height, output_width, output_height,
                 y, y_stride, u, u_stride, v, v_stride, false);
}

void rgba_to_nv12(const std::uint8_t* rgba, int source_width, int source_height,
                  std::uint8_t* y, int y_stride,
                  std::uint8_t* uv, int uv_stride,
                  int output_width, int output_height) {
    convert_rgba(rgba, source_width, source_height, output_width, output_height,
                 y, y_stride, uv, uv_stride, nullptr, 0, true);
}

struct VideoEncoder::Impl {
    explicit Impl(VideoEncoderSettings value) : settings(value) {}
    ~Impl() { cleanup(); }

    VideoEncoderSettings settings;
    AVFormatContext* format = nullptr;
    AVCodecContext* video = nullptr;
    AVCodecContext* audio = nullptr;
    AVStream* video_stream = nullptr;
    AVStream* audio_stream = nullptr;
    AVFrame* video_frame = nullptr;
    AVFrame* audio_frame = nullptr;
    AVPacket* packet = nullptr;
    bool opened = false;
    bool header_written = false;
    bool finished = false;
    std::int64_t video_pts = 0;
    std::int64_t audio_pts = 0;
    std::vector<std::int16_t> audio_pending;
    std::size_t audio_read = 0;

    void cleanup() {
        av_packet_free(&packet);
        av_frame_free(&video_frame);
        av_frame_free(&audio_frame);
        avcodec_free_context(&video);
        avcodec_free_context(&audio);
        if (format != nullptr) {
            if (format->pb != nullptr) {
                avio_closep(&format->pb);
            }
            avformat_free_context(format);
            format = nullptr;
        }
        opened = false;
        header_written = false;
    }

    bool receive_packets(AVCodecContext* codec, AVStream* stream, std::string* error) {
        for (;;) {
            const int result = avcodec_receive_packet(codec, packet);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return true;
            }
            if (result < 0) {
                return set_error(ff_error(result), error);
            }
            av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
            packet->stream_index = stream->index;
            const int written = av_interleaved_write_frame(format, packet);
            av_packet_unref(packet);
            if (written < 0) {
                return set_error(ff_error(written), error);
            }
        }
    }

    bool encode_frame(AVCodecContext* codec, AVStream* stream, AVFrame* frame,
                      std::string* error) {
        int result = avcodec_send_frame(codec, frame);
        int attempts = 0;
        while (result == AVERROR(EAGAIN) && attempts++ < 256) {
            if (!receive_packets(codec, stream, error)) {
                return false;
            }
            result = avcodec_send_frame(codec, frame);
        }
        if (result < 0) {
            return set_error(ff_error(result), error);
        }
        return receive_packets(codec, stream, error);
    }

    bool open(const std::string& path, std::string* error);
    bool write_video_frame(const std::uint8_t* rgba, std::string* error);
    bool write_audio(const float* samples, std::size_t frames, std::string* error);
    bool finish(std::string* error);
};

bool VideoEncoder::Impl::open(const std::string& path, std::string* error) {
    if (opened) {
        return set_error("encoder is already open", error);
    }
    const VideoFormatInfo& info = video_format_info(settings.format);
    settings.width = std::max(settings.width, 2);
    settings.height = std::max(settings.height, 2);
    settings.fps = std::max(settings.fps, 1);
    const int output_width = settings.width + (settings.width & 1);
    const int output_height = settings.height + (settings.height & 1);

    int result = avformat_alloc_output_context2(&format, nullptr, info.muxer, path.c_str());
    if (result < 0 || format == nullptr) {
        return set_error("cannot create the output format: " + ff_error(result), error);
    }

    const AVCodec* video_codec = avcodec_find_encoder_by_name(info.video_encoder);
    if (video_codec == nullptr) {
        return set_error(std::string("video encoder is not available: ") + info.video_encoder,
                         error);
    }
    video_stream = avformat_new_stream(format, nullptr);
    if (video_stream == nullptr) {
        return set_error("cannot create the video stream", error);
    }
    video = avcodec_alloc_context3(video_codec);
    if (video == nullptr) {
        return set_error("cannot allocate the video encoder", error);
    }
    video->width = output_width;
    video->height = output_height;
    video->time_base = AVRational{1, settings.fps};
    video->framerate = AVRational{settings.fps, 1};
    video->pix_fmt = std::strcmp(info.video_encoder, "mpeg4") == 0 ? AV_PIX_FMT_YUV420P
                                                                   : AV_PIX_FMT_NV12;
    video->bit_rate = std::max(settings.video_bitrate, 100'000);
    video->gop_size = settings.fps * 2;
    video->max_b_frames = 0;
    video->color_range = AVCOL_RANGE_MPEG;
    video->colorspace = AVCOL_SPC_BT709;
    video->color_primaries = AVCOL_PRI_BT709;
    video->color_trc = AVCOL_TRC_BT709;
    if ((format->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
        video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (std::strcmp(info.video_encoder, "h264_mf") == 0) {
        video->profile = AV_PROFILE_H264_MAIN;
    } else if (std::strcmp(info.video_encoder, "hevc_mf") == 0) {
        video->profile = AV_PROFILE_HEVC_MAIN;
    }
    result = avcodec_open2(video, video_codec, nullptr);
    if (result < 0) {
        return set_error("cannot open the video encoder: " + ff_error(result), error);
    }
    result = avcodec_parameters_from_context(video_stream->codecpar, video);
    if (result < 0) {
        return set_error("cannot copy the video parameters: " + ff_error(result), error);
    }
    video_stream->time_base = video->time_base;

    if (settings.include_audio) {
        const AVCodec* audio_codec = avcodec_find_encoder_by_name(info.audio_encoder);
        if (audio_codec == nullptr) {
            return set_error(std::string("audio encoder is not available: ") + info.audio_encoder,
                             error);
        }
        audio_stream = avformat_new_stream(format, nullptr);
        if (audio_stream == nullptr) {
            return set_error("cannot create the audio stream", error);
        }
        audio = avcodec_alloc_context3(audio_codec);
        if (audio == nullptr) {
            return set_error("cannot allocate the audio encoder", error);
        }
        audio->sample_rate = 48'000;
        audio->sample_fmt = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&audio->ch_layout, 2);
        audio->bit_rate = std::max(settings.audio_bitrate, 32'000);
        audio->time_base = AVRational{1, audio->sample_rate};
        if ((format->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
            audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        result = avcodec_open2(audio, audio_codec, nullptr);
        if (result < 0) {
            return set_error("cannot open the audio encoder: " + ff_error(result), error);
        }
        result = avcodec_parameters_from_context(audio_stream->codecpar, audio);
        if (result < 0) {
            return set_error("cannot copy the audio parameters: " + ff_error(result), error);
        }
        audio_stream->time_base = audio->time_base;

        audio_frame = av_frame_alloc();
        if (audio_frame == nullptr) {
            return set_error("cannot allocate the audio frame", error);
        }
        audio_frame->nb_samples = audio->frame_size > 0 ? audio->frame_size : 1024;
        audio_frame->format = audio->sample_fmt;
        audio_frame->sample_rate = audio->sample_rate;
        av_channel_layout_copy(&audio_frame->ch_layout, &audio->ch_layout);
        result = av_frame_get_buffer(audio_frame, 0);
        if (result < 0) {
            return set_error("cannot allocate the audio buffer: " + ff_error(result), error);
        }
    }

    video_frame = av_frame_alloc();
    if (video_frame == nullptr) {
        return set_error("cannot allocate the video frame", error);
    }
    video_frame->format = video->pix_fmt;
    video_frame->width = output_width;
    video_frame->height = output_height;
    video_frame->color_range = AVCOL_RANGE_MPEG;
    video_frame->colorspace = AVCOL_SPC_BT709;
    video_frame->color_primaries = AVCOL_PRI_BT709;
    video_frame->color_trc = AVCOL_TRC_BT709;
    result = av_frame_get_buffer(video_frame, 32);
    if (result < 0) {
        return set_error("cannot allocate the video buffer: " + ff_error(result), error);
    }

    packet = av_packet_alloc();
    if (packet == nullptr) {
        return set_error("cannot allocate the packet", error);
    }

    result = avio_open(&format->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (result < 0) {
        return set_error("cannot open the output file: " + ff_error(result), error);
    }
    result = avformat_write_header(format, nullptr);
    if (result < 0) {
        return set_error("cannot write the file header: " + ff_error(result), error);
    }
    header_written = true;
    opened = true;
    return true;
}

bool VideoEncoder::Impl::write_video_frame(const std::uint8_t* rgba, std::string* error) {
    if (!opened || video_frame == nullptr) {
        return set_error("encoder is not open", error);
    }
    const int result = av_frame_make_writable(video_frame);
    if (result < 0) {
        return set_error(ff_error(result), error);
    }
    if (video->pix_fmt == AV_PIX_FMT_NV12) {
        rgba_to_nv12(rgba, settings.width, settings.height,
                     video_frame->data[0], video_frame->linesize[0],
                     video_frame->data[1], video_frame->linesize[1],
                     video->width, video->height);
    } else {
        rgba_to_yuv420p(rgba, settings.width, settings.height,
                        video_frame->data[0], video_frame->linesize[0],
                        video_frame->data[1], video_frame->linesize[1],
                        video_frame->data[2], video_frame->linesize[2],
                        video->width, video->height);
    }
    video_frame->pts = video_pts++;
    return encode_frame(video, video_stream, video_frame, error);
}

bool VideoEncoder::Impl::write_audio(const float* samples, std::size_t frames,
                                     std::string* error) {
    if (!opened) {
        return set_error("encoder is not open", error);
    }
    if (audio == nullptr || audio_frame == nullptr) {
        return true;
    }
    const std::size_t base = audio_pending.size();
    audio_pending.resize(base + frames * 2);
    for (std::size_t i = 0; i < frames * 2; ++i) {
        const float value = std::clamp(samples[i], -1.0f, 1.0f);
        audio_pending[base + i] = static_cast<std::int16_t>(std::lround(value * 32767.0f));
    }

    const std::size_t frame_size = static_cast<std::size_t>(audio_frame->nb_samples);
    while (audio_pending.size() / 2 - audio_read >= frame_size) {
        const int result = av_frame_make_writable(audio_frame);
        if (result < 0) {
            return set_error(ff_error(result), error);
        }
        std::memcpy(audio_frame->data[0], audio_pending.data() + audio_read * 2,
                    frame_size * 2 * sizeof(std::int16_t));
        audio_frame->pts = audio_pts;
        audio_pts += static_cast<std::int64_t>(frame_size);
        if (!encode_frame(audio, audio_stream, audio_frame, error)) {
            return false;
        }
        audio_read += frame_size;
    }
    if (audio_read > 48'000) {
        audio_pending.erase(audio_pending.begin(),
                            audio_pending.begin() + static_cast<std::ptrdiff_t>(audio_read * 2));
        audio_read = 0;
    }
    return true;
}

bool VideoEncoder::Impl::finish(std::string* error) {
    if (!opened || finished) {
        return true;
    }
    bool ok = true;
    if (video != nullptr) {
        int result = avcodec_send_frame(video, nullptr);
        if (result < 0 && result != AVERROR_EOF) {
            ok = set_error(ff_error(result), error);
        }
        if (ok) {
            ok = receive_packets(video, video_stream, error);
        }
    }

    if (ok && audio != nullptr && audio_frame != nullptr) {
        const std::size_t frame_size = static_cast<std::size_t>(audio_frame->nb_samples);
        while (audio_pending.size() / 2 - audio_read > 0) {
            const std::size_t available = audio_pending.size() / 2 - audio_read;
            const std::size_t take = std::min(available, frame_size);
            const int result = av_frame_make_writable(audio_frame);
            if (result < 0) {
                ok = set_error(ff_error(result), error);
                break;
            }
            std::memcpy(audio_frame->data[0], audio_pending.data() + audio_read * 2,
                        take * 2 * sizeof(std::int16_t));
            if (take < frame_size) {
                std::memset(audio_frame->data[0] + take * 2 * sizeof(std::int16_t), 0,
                            (frame_size - take) * 2 * sizeof(std::int16_t));
            }
            audio_frame->pts = audio_pts;
            audio_pts += static_cast<std::int64_t>(frame_size);
            if (!encode_frame(audio, audio_stream, audio_frame, error)) {
                ok = false;
                break;
            }
            audio_read += take;
        }
        if (ok) {
            const int result = avcodec_send_frame(audio, nullptr);
            if (result < 0 && result != AVERROR_EOF) {
                ok = set_error(ff_error(result), error);
            }
            if (ok) {
                ok = receive_packets(audio, audio_stream, error);
            }
        }
    }

    if (ok && header_written) {
        const int result = av_write_trailer(format);
        if (result < 0) {
            ok = set_error("cannot finalize the file: " + ff_error(result), error);
        }
    }
    if (format != nullptr && format->pb != nullptr) {
        avio_closep(&format->pb);
    }
    finished = true;
    return ok;
}

VideoEncoder::VideoEncoder(VideoEncoderSettings settings)
    : impl_(std::make_unique<Impl>(settings)) {}

VideoEncoder::~VideoEncoder() = default;

bool VideoEncoder::open(const std::string& utf8_path, std::string* error) {
    return impl_->open(utf8_path, error);
}

bool VideoEncoder::write_video_frame(const std::uint8_t* rgba, std::string* error) {
    return impl_->write_video_frame(rgba, error);
}

bool VideoEncoder::write_audio(const float* interleaved_stereo, std::size_t sample_frames,
                               std::string* error) {
    return impl_->write_audio(interleaved_stereo, sample_frames, error);
}

bool VideoEncoder::finish(std::string* error) {
    return impl_->finish(error);
}

bool VideoEncoder::has_audio() const {
    return impl_->audio != nullptr;
}

struct VideoEncoderThread::Impl {
    enum class Command { Open, Video, Audio, Finish };

    VideoEncoderSettings settings;
    std::string path;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable completed;
    Command command = Command::Open;
    const std::uint8_t* rgba = nullptr;
    const float* samples = nullptr;
    std::size_t frames = 0;
    bool pending = false;
    bool done = false;
    bool stop = false;
    bool result = false;
    std::string error;
    std::unique_ptr<VideoEncoder> encoder;

    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!thread.joinable()) {
                return;
            }
            stop = true;
            pending = true;
        }
        wake.notify_all();
        thread.join();
    }

    void run() {
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return pending || stop; });
            if (stop) {
                return;
            }
            const Command current = command;
            const std::uint8_t* frame = rgba;
            const float* audio = samples;
            const std::size_t audio_frames = frames;
            pending = false;
            lock.unlock();

            bool ok = true;
            std::string message;
            try {
                switch (current) {
                case Command::Open:
                    encoder = std::make_unique<VideoEncoder>(settings);
                    ok = encoder->open(path, &message);
                    break;
                case Command::Video:
                    ok = encoder != nullptr && encoder->write_video_frame(frame, &message);
                    if (encoder == nullptr) {
                        message = "encoder is not open";
                    }
                    break;
                case Command::Audio:
                    ok = encoder != nullptr && encoder->write_audio(audio, audio_frames, &message);
                    if (encoder == nullptr) {
                        message = "encoder is not open";
                    }
                    break;
                case Command::Finish:
                    ok = encoder != nullptr && encoder->finish(&message);
                    if (encoder == nullptr) {
                        message = "encoder is not open";
                    }
                    break;
                }
            } catch (const std::exception& exception) {
                ok = false;
                message = exception.what();
            } catch (...) {
                ok = false;
                message = "unknown encoder error";
            }

            lock.lock();
            result = ok;
            error = std::move(message);
            done = true;
            completed.notify_all();
        }
    }

    bool submit(Command next, const std::uint8_t* frame, const float* audio,
                std::size_t audio_frames, std::string* error_out) {
        std::unique_lock<std::mutex> lock(mutex);
        if (!thread.joinable()) {
            if (error_out != nullptr) {
                *error_out = "encoder thread is not running";
            }
            return false;
        }
        command = next;
        rgba = frame;
        samples = audio;
        frames = audio_frames;
        done = false;
        pending = true;
        wake.notify_all();
        completed.wait(lock, [this] { return done; });
        if (!result && error_out != nullptr) {
            *error_out = error;
        }
        return result;
    }
};

VideoEncoderThread::VideoEncoderThread() : impl_(std::make_unique<Impl>()) {}

VideoEncoderThread::~VideoEncoderThread() = default;

bool VideoEncoderThread::open(const std::string& utf8_path,
                              const VideoEncoderSettings& settings, std::string* error) {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->settings = settings;
        impl_->path = utf8_path;
        if (!impl_->thread.joinable()) {
            impl_->thread = std::thread([impl = impl_.get()] { impl->run(); });
        }
    }
    return impl_->submit(Impl::Command::Open, nullptr, nullptr, 0, error);
}

bool VideoEncoderThread::write_video_frame(const std::uint8_t* rgba, std::string* error) {
    return impl_->submit(Impl::Command::Video, rgba, nullptr, 0, error);
}

bool VideoEncoderThread::write_audio(const float* interleaved_stereo, std::size_t sample_frames,
                                     std::string* error) {
    return impl_->submit(Impl::Command::Audio, nullptr, interleaved_stereo, sample_frames,
                         error);
}

bool VideoEncoderThread::finish(std::string* error) {
    return impl_->submit(Impl::Command::Finish, nullptr, nullptr, 0, error);
}

} // namespace hikari::video

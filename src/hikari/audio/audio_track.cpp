#include "hikari/audio/audio_track.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace hikari::audio {
namespace {

std::string av_error_string(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

struct FormatCloser {
    void operator()(AVFormatContext* context) const {
        if (context != nullptr) {
            avformat_close_input(&context);
        }
    }
};

struct CodecCloser {
    void operator()(AVCodecContext* context) const {
        if (context != nullptr) {
            avcodec_free_context(&context);
        }
    }
};

struct FrameCloser {
    void operator()(AVFrame* frame) const {
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
    }
};

struct PacketCloser {
    void operator()(AVPacket* packet) const {
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
    }
};

struct SwrCloser {
    void operator()(SwrContext* context) const {
        if (context != nullptr) {
            swr_free(&context);
        }
    }
};

} // namespace

DecodeResult decode_audio_file(const std::string& utf8_path) {
    DecodeResult result;
    AVFormatContext* format_raw = nullptr;
    const int open_result = avformat_open_input(&format_raw, utf8_path.c_str(), nullptr, nullptr);
    if (open_result < 0 || format_raw == nullptr) {
        result.error = "cannot open audio file: " + av_error_string(open_result);
        return result;
    }
    std::unique_ptr<AVFormatContext, FormatCloser> format(format_raw);

    int stream_result = avformat_find_stream_info(format.get(), nullptr);
    if (stream_result < 0) {
        result.error = "cannot read audio stream info: " + av_error_string(stream_result);
        return result;
    }

    const AVCodec* codec = nullptr;
    const int stream_index =
        av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (stream_index < 0 || codec == nullptr) {
        result.error = "audio stream not found";
        return result;
    }

    std::unique_ptr<AVCodecContext, CodecCloser> context(avcodec_alloc_context3(codec));
    if (!context) {
        result.error = "cannot allocate audio decoder";
        return result;
    }
    stream_result = avcodec_parameters_to_context(
        context.get(), format->streams[stream_index]->codecpar);
    if (stream_result < 0) {
        result.error = "cannot initialize audio decoder: " + av_error_string(stream_result);
        return result;
    }
    stream_result = avcodec_open2(context.get(), codec, nullptr);
    if (stream_result < 0) {
        result.error = "cannot open audio decoder: " + av_error_string(stream_result);
        return result;
    }

    AVChannelLayout input_layout{};
    const AVChannelLayout* layout = &context->ch_layout;
    if (layout->nb_channels == 0) {
        av_channel_layout_default(&input_layout, kOutputChannels);
        layout = &input_layout;
    }
    AVChannelLayout output_layout{};
    av_channel_layout_default(&output_layout, kOutputChannels);

    SwrContext* swr_raw = nullptr;
    const int swr_result = swr_alloc_set_opts2(
        &swr_raw, &output_layout, AV_SAMPLE_FMT_FLT, kOutputSampleRate, layout,
        context->sample_fmt, context->sample_rate, 0, nullptr);
    if (swr_result < 0 || swr_raw == nullptr) {
        result.error = "cannot create audio resampler: " + av_error_string(swr_result);
        av_channel_layout_uninit(&input_layout);
        av_channel_layout_uninit(&output_layout);
        return result;
    }
    std::unique_ptr<SwrContext, SwrCloser> swr(swr_raw);
    if (swr_init(swr.get()) < 0) {
        result.error = "cannot initialize audio resampler";
        av_channel_layout_uninit(&input_layout);
        av_channel_layout_uninit(&output_layout);
        return result;
    }

    std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketCloser> packet(av_packet_alloc());
    if (!frame || !packet) {
        result.error = "cannot allocate audio buffers";
        av_channel_layout_uninit(&input_layout);
        av_channel_layout_uninit(&output_layout);
        return result;
    }

    AudioTrack& track = result.track;
    track.path = utf8_path;

    const auto append_frame = [&]() -> bool {
        const int capacity = static_cast<int>(av_rescale_rnd(
            swr_get_delay(swr.get(), context->sample_rate) + frame->nb_samples,
            kOutputSampleRate, context->sample_rate, AV_ROUND_UP));
        if (capacity <= 0) {
            return true;
        }
        const std::size_t offset = track.samples.size();
        track.samples.resize(offset + static_cast<std::size_t>(capacity) * kOutputChannels);
        uint8_t* output[] = {
            reinterpret_cast<uint8_t*>(track.samples.data() + offset)};
        const uint8_t* const* input = frame->extended_data;
        const int converted =
            swr_convert(swr.get(), output, capacity, input, frame->nb_samples);
        if (converted < 0) {
            result.error = "audio resampling failed";
            return false;
        }
        track.samples.resize(offset + static_cast<std::size_t>(converted) * kOutputChannels);
        return true;
    };

    const auto drain_decoder = [&]() -> bool {
        while (true) {
            const int receive = avcodec_receive_frame(context.get(), frame.get());
            if (receive == AVERROR(EAGAIN) || receive == AVERROR_EOF) {
                return true;
            }
            if (receive < 0) {
                result.error = "audio decode failed: " + av_error_string(receive);
                return false;
            }
            if (!append_frame()) {
                return false;
            }
            av_frame_unref(frame.get());
        }
    };

    while (true) {
        const int read = av_read_frame(format.get(), packet.get());
        if (read < 0) {
            break;
        }
        if (packet->stream_index == stream_index) {
            int send = avcodec_send_packet(context.get(), packet.get());
            if (send == AVERROR(EAGAIN)) {
                if (!drain_decoder()) {
                    av_packet_unref(packet.get());
                    av_channel_layout_uninit(&input_layout);
                    av_channel_layout_uninit(&output_layout);
                    return result;
                }
                send = avcodec_send_packet(context.get(), packet.get());
            }
            if (send < 0 && send != AVERROR(EAGAIN)) {
                result.error = "audio demux failed: " + av_error_string(send);
                av_packet_unref(packet.get());
                av_channel_layout_uninit(&input_layout);
                av_channel_layout_uninit(&output_layout);
                return result;
            }
            if (send >= 0 && !drain_decoder()) {
                av_packet_unref(packet.get());
                av_channel_layout_uninit(&input_layout);
                av_channel_layout_uninit(&output_layout);
                return result;
            }
        }
        av_packet_unref(packet.get());
    }

    const int flush = avcodec_send_packet(context.get(), nullptr);
    if (flush < 0 && flush != AVERROR_EOF) {
        result.error = "audio decoder flush failed: " + av_error_string(flush);
        av_channel_layout_uninit(&input_layout);
        av_channel_layout_uninit(&output_layout);
        return result;
    }
    if (!drain_decoder()) {
        av_channel_layout_uninit(&input_layout);
        av_channel_layout_uninit(&output_layout);
        return result;
    }

    while (true) {
        const int capacity = static_cast<int>(av_rescale_rnd(
            swr_get_delay(swr.get(), kOutputSampleRate), kOutputSampleRate,
            kOutputSampleRate, AV_ROUND_UP));
        if (capacity <= 0) {
            break;
        }
        const std::size_t offset = track.samples.size();
        track.samples.resize(offset + static_cast<std::size_t>(capacity) * kOutputChannels);
        uint8_t* output[] = {
            reinterpret_cast<uint8_t*>(track.samples.data() + offset)};
        const int converted = swr_convert(swr.get(), output, capacity, nullptr, 0);
        if (converted <= 0) {
            track.samples.resize(offset);
            break;
        }
        track.samples.resize(offset + static_cast<std::size_t>(converted) * kOutputChannels);
    }

    av_channel_layout_uninit(&input_layout);
    av_channel_layout_uninit(&output_layout);

    if (track.samples.empty()) {
        result.error = "audio stream is empty";
        return result;
    }

    track.frame_count = static_cast<std::int64_t>(track.samples.size() / kOutputChannels);
    track.duration_seconds =
        static_cast<double>(track.frame_count) / static_cast<double>(kOutputSampleRate);

    const std::size_t block_count =
        (static_cast<std::size_t>(track.frame_count) + kPeakBlockFrames - 1) /
        kPeakBlockFrames;
    track.peaks.resize(block_count, 0.0f);
    for (std::size_t block = 0; block < block_count; ++block) {
        const std::size_t begin = block * kPeakBlockFrames;
        const std::size_t end =
            std::min(begin + kPeakBlockFrames, static_cast<std::size_t>(track.frame_count));
        float peak = 0.0f;
        for (std::size_t index = begin; index < end; ++index) {
            const std::size_t sample = index * kOutputChannels;
            peak = std::max(peak, std::abs(track.samples[sample]));
            peak = std::max(peak, std::abs(track.samples[sample + 1]));
        }
        track.peaks[block] = peak;
    }

    return result;
}

} // namespace hikari::audio

#include "audio/FfmpegAudioSource.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

// FFmpeg 5.1 replaced the channel-count/channel-mask fields with AVChannelLayout (and 7.0 removed the old ones).
#define GRIM_FFMPEG_CH_LAYOUT (LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100))

struct FfmpegAudioSource::State
{
    ma_data_source_base base{}; // must stay first: miniaudio casts the data source pointer to it

    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    SwrContext *resampler = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    int stream = -1;
    ma_uint32 channels = 0;
    ma_uint32 sampleRate = 0;
    ma_uint64 lengthFrames = 0;
    ma_uint64 cursor = 0;
    bool drained = false; // the decoder has been flushed at end of file

    std::vector<float> pending; // decoded samples not yet handed out
    size_t pendingOffset = 0;   // in samples
    // Seeking lands on a packet at or before the target; drop the gap once.
    bool trimAfterSeek = false;
    ma_uint64 seekTarget = 0;

    ~State()
    {
        swr_free(&resampler);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }

    // Converts the decoded frame to interleaved float into `pending`.
    void takeFrame()
    {
        const int count = frame->nb_samples;
        pending.resize(static_cast<size_t>(count) * channels);
        auto *out = reinterpret_cast<uint8_t *>(pending.data());
        const int converted = swr_convert(resampler, &out, count, const_cast<const uint8_t **>(frame->extended_data),
                                          count);
        pending.resize(static_cast<size_t>(std::max(converted, 0)) * channels);
        pendingOffset = 0;
        if (trimAfterSeek && frame->best_effort_timestamp != AV_NOPTS_VALUE) {
            trimAfterSeek = false;
            const AVRational timeBase = format->streams[stream]->time_base;
            const auto frameStart = static_cast<ma_uint64>(
                std::max<int64_t>(0, av_rescale_q(frame->best_effort_timestamp, timeBase, AVRational{1, int(sampleRate)})));
            if (seekTarget > frameStart)
                pendingOffset = std::min<size_t>((seekTarget - frameStart) * channels, pending.size());
        }
    }

    // Decodes the next frame into `pending`. False at end of stream or on error.
    bool fill()
    {
        for (;;) {
            const int received = avcodec_receive_frame(codec, frame);
            if (received == 0) {
                takeFrame();
                return true;
            }
            if (received != AVERROR(EAGAIN) || drained)
                return false;
            const int read = av_read_frame(format, packet);
            if (read < 0) {
                drained = true;
                avcodec_send_packet(codec, nullptr); // flush the frames the decoder still holds
                continue;
            }
            if (packet->stream_index == stream)
                avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
        }
    }
};

namespace {

FfmpegAudioSource::State *stateOf(ma_data_source *dataSource)
{
    return reinterpret_cast<FfmpegAudioSource::State *>(dataSource);
}

ma_result onRead(ma_data_source *dataSource, void *framesOut, ma_uint64 frameCount, ma_uint64 *framesRead)
{
    FfmpegAudioSource::State *state = stateOf(dataSource);
    auto *out = static_cast<float *>(framesOut);
    ma_uint64 done = 0;
    while (done < frameCount) {
        if (state->pendingOffset >= state->pending.size()) {
            if (!state->fill())
                break;
            continue;
        }
        const ma_uint64 available = (state->pending.size() - state->pendingOffset) / state->channels;
        const ma_uint64 count = std::min(frameCount - done, available);
        std::memcpy(out + done * state->channels, state->pending.data() + state->pendingOffset,
                    count * state->channels * sizeof(float));
        state->pendingOffset += count * state->channels;
        done += count;
    }
    state->cursor += done;
    if (framesRead)
        *framesRead = done;
    return done == 0 ? MA_AT_END : MA_SUCCESS;
}

ma_result onSeek(ma_data_source *dataSource, ma_uint64 frameIndex)
{
    FfmpegAudioSource::State *state = stateOf(dataSource);
    const AVRational timeBase = state->format->streams[state->stream]->time_base;
    const int64_t timestamp = av_rescale_q(static_cast<int64_t>(frameIndex), AVRational{1, int(state->sampleRate)},
                                           timeBase);
    if (av_seek_frame(state->format, state->stream, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
        return MA_ERROR;
    avcodec_flush_buffers(state->codec);
    state->drained = false;
    state->pending.clear();
    state->pendingOffset = 0;
    state->cursor = frameIndex;
    state->seekTarget = frameIndex;
    state->trimAfterSeek = true;
    return MA_SUCCESS;
}

ma_result onGetDataFormat(ma_data_source *dataSource, ma_format *format, ma_uint32 *channels, ma_uint32 *sampleRate,
                          ma_channel *channelMap, size_t channelMapCap)
{
    const FfmpegAudioSource::State *state = stateOf(dataSource);
    if (format)
        *format = ma_format_f32;
    if (channels)
        *channels = state->channels;
    if (sampleRate)
        *sampleRate = state->sampleRate;
    if (channelMap)
        ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCap, state->channels);
    return MA_SUCCESS;
}

ma_result onGetCursor(ma_data_source *dataSource, ma_uint64 *cursor)
{
    *cursor = stateOf(dataSource)->cursor;
    return MA_SUCCESS;
}

ma_result onGetLength(ma_data_source *dataSource, ma_uint64 *length)
{
    *length = stateOf(dataSource)->lengthFrames;
    return MA_SUCCESS;
}

ma_data_source_vtable kVtable = {onRead, onSeek, onGetDataFormat, onGetCursor, onGetLength, nullptr, 0};

} // namespace

bool FfmpegAudioSource::handles(const std::filesystem::path &file)
{
    std::string extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".m4a" || extension == ".mp4" || extension == ".aac";
}

std::unique_ptr<FfmpegAudioSource> FfmpegAudioSource::open(const std::filesystem::path &file)
{
    av_log_set_level(AV_LOG_ERROR);
    auto state = std::make_unique<State>();
    if (avformat_open_input(&state->format, file.c_str(), nullptr, nullptr) < 0
        || avformat_find_stream_info(state->format, nullptr) < 0)
        return nullptr;
#if LIBAVFORMAT_VERSION_MAJOR >= 59
    const AVCodec *decoder = nullptr;
#else
    AVCodec *decoder = nullptr; // FFmpeg 4 (Ubuntu 22.04) takes a non-const pointer here
#endif
    state->stream = av_find_best_stream(state->format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (state->stream < 0 || !decoder)
        return nullptr;
    AVStream *stream = state->format->streams[state->stream];
    state->codec = avcodec_alloc_context3(decoder);
    if (!state->codec || avcodec_parameters_to_context(state->codec, stream->codecpar) < 0
        || avcodec_open2(state->codec, decoder, nullptr) < 0)
        return nullptr;
    state->sampleRate = static_cast<ma_uint32>(state->codec->sample_rate);

#if GRIM_FFMPEG_CH_LAYOUT
    AVChannelLayout layout{};
    if (state->codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&layout, state->codec->ch_layout.nb_channels);
    else
        av_channel_layout_copy(&layout, &state->codec->ch_layout);
    state->channels = static_cast<ma_uint32>(layout.nb_channels);
    const int made = swr_alloc_set_opts2(&state->resampler, &layout, AV_SAMPLE_FMT_FLT, state->codec->sample_rate,
                                         &layout, state->codec->sample_fmt, state->codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&layout);
    if (made < 0)
        return nullptr;
#else
    const int channelCount = state->codec->channels;
    const int64_t layout = state->codec->channel_layout ? static_cast<int64_t>(state->codec->channel_layout)
                                                        : av_get_default_channel_layout(channelCount);
    state->channels = static_cast<ma_uint32>(channelCount);
    state->resampler = swr_alloc_set_opts(nullptr, layout, AV_SAMPLE_FMT_FLT, state->codec->sample_rate, layout,
                                          state->codec->sample_fmt, state->codec->sample_rate, 0, nullptr);
#endif
    if (state->channels == 0 || state->sampleRate == 0 || !state->resampler || swr_init(state->resampler) < 0)
        return nullptr;

    state->packet = av_packet_alloc();
    state->frame = av_frame_alloc();
    if (!state->packet || !state->frame)
        return nullptr;

    if (stream->duration != AV_NOPTS_VALUE)
        state->lengthFrames = static_cast<ma_uint64>(
            av_rescale_q(stream->duration, stream->time_base, AVRational{1, int(state->sampleRate)}));
    else if (state->format->duration != AV_NOPTS_VALUE)
        state->lengthFrames = static_cast<ma_uint64>(state->format->duration) * state->sampleRate / AV_TIME_BASE;

    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &kVtable;
    if (ma_data_source_init(&config, &state->base) != MA_SUCCESS)
        return nullptr;
    return std::unique_ptr<FfmpegAudioSource>(new FfmpegAudioSource(std::move(state)));
}

FfmpegAudioSource::FfmpegAudioSource(std::unique_ptr<State> state)
    : m_state(std::move(state))
{
}

FfmpegAudioSource::~FfmpegAudioSource()
{
    ma_data_source_uninit(&m_state->base);
}

ma_data_source *FfmpegAudioSource::dataSource() const
{
    return &m_state->base;
}

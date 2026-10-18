#include "audio/MfAudioSource.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr LONGLONG kHundredNsPerSecond = 10'000'000;
constexpr DWORD kAudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

} // namespace

struct MfAudioSource::State
{
    ma_data_source_base base{}; // must stay first: miniaudio casts the data source pointer to it

    ComPtr<IMFSourceReader> reader;
    ma_uint32 channels = 0;
    ma_uint32 sampleRate = 0;
    ma_uint64 lengthFrames = 0;
    ma_uint64 cursor = 0;

    std::vector<float> pending; // decoded samples not yet handed out
    size_t pendingOffset = 0;   // in samples
    // Media Foundation seeks to a point at or before the target; drop the gap once.
    bool trimAfterSeek = false;
    ma_uint64 seekTarget = 0;

    // Decodes the next compressed packet into `pending`. False at end of stream or on error.
    bool fill()
    {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader->ReadSample(kAudioStream, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
            return false;
        pending.clear();
        pendingOffset = 0;
        if (!sample)
            return true; // gap or format tick; caller reads again

        ComPtr<IMFMediaBuffer> buffer;
        BYTE *data = nullptr;
        DWORD bytes = 0;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || FAILED(buffer->Lock(&data, nullptr, &bytes)))
            return false;
        const auto *samples = reinterpret_cast<const float *>(data);
        pending.assign(samples, samples + bytes / sizeof(float));
        buffer->Unlock();

        if (trimAfterSeek) {
            trimAfterSeek = false;
            const auto packetFrame = static_cast<ma_uint64>(timestamp) * sampleRate / kHundredNsPerSecond;
            if (seekTarget > packetFrame)
                pendingOffset = std::min<size_t>((seekTarget - packetFrame) * channels, pending.size());
        }
        return true;
    }
};

namespace {

MfAudioSource::State *stateOf(ma_data_source *dataSource)
{
    return reinterpret_cast<MfAudioSource::State *>(dataSource);
}

ma_result onRead(ma_data_source *dataSource, void *framesOut, ma_uint64 frameCount, ma_uint64 *framesRead)
{
    MfAudioSource::State *state = stateOf(dataSource);
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
    MfAudioSource::State *state = stateOf(dataSource);
    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = static_cast<LONGLONG>(frameIndex * kHundredNsPerSecond / state->sampleRate);
    if (FAILED(state->reader->SetCurrentPosition(GUID_NULL, position)))
        return MA_ERROR;
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
    const MfAudioSource::State *state = stateOf(dataSource);
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

bool MfAudioSource::handles(const std::filesystem::path &file)
{
    std::wstring extension = file.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
    return extension == L".m4a" || extension == L".mp4" || extension == L".aac";
}

std::unique_ptr<MfAudioSource> MfAudioSource::open(const std::filesystem::path &file)
{
    static std::once_flag started;
    std::call_once(started, [] { MFStartup(MF_VERSION, MFSTARTUP_LITE); });
    // Media Foundation needs COM on the opening thread; RPC_E_CHANGED_MODE (already STA) is fine too.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    auto state = std::make_unique<State>();
    if (FAILED(MFCreateSourceReaderFromURL(file.c_str(), nullptr, &state->reader)))
        return nullptr;
    state->reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    state->reader->SetStreamSelection(kAudioStream, TRUE);

    // Ask for 32-bit float PCM; Media Foundation inserts the decoder.
    ComPtr<IMFMediaType> wanted;
    if (FAILED(MFCreateMediaType(&wanted)))
        return nullptr;
    wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    ComPtr<IMFMediaType> actual;
    if (FAILED(state->reader->SetCurrentMediaType(kAudioStream, nullptr, wanted.Get()))
        || FAILED(state->reader->GetCurrentMediaType(kAudioStream, &actual)))
        return nullptr;
    state->channels = MFGetAttributeUINT32(actual.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
    state->sampleRate = MFGetAttributeUINT32(actual.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    if (state->channels == 0 || state->sampleRate == 0)
        return nullptr;

    PROPVARIANT duration;
    PropVariantInit(&duration);
    if (SUCCEEDED(state->reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
                                                          MF_PD_DURATION, &duration))) {
        state->lengthFrames = duration.uhVal.QuadPart * state->sampleRate / kHundredNsPerSecond;
        PropVariantClear(&duration);
    }

    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &kVtable;
    if (ma_data_source_init(&config, &state->base) != MA_SUCCESS)
        return nullptr;
    return std::unique_ptr<MfAudioSource>(new MfAudioSource(std::move(state)));
}

MfAudioSource::MfAudioSource(std::unique_ptr<State> state)
    : m_state(std::move(state))
{
}

MfAudioSource::~MfAudioSource()
{
    ma_data_source_uninit(&m_state->base);
}

ma_data_source *MfAudioSource::dataSource() const
{
    return &m_state->base;
}

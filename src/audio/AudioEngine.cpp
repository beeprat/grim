#include "audio/AudioEngine.h"
#include "audio/AacSource.h"
#include "audio/Reverb.h"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace {

// One loaded track. Referenced by miniaudio's threads, so it must never move.
struct Slot
{
    ma_sound sound{};
    bool loaded = false;
    // Set for formats miniaudio cannot decode (AAC/M4A); must outlive `sound`.
    std::unique_ptr<AacSource> mfSource;

    void unload()
    {
        if (!loaded)
            return;
        ma_sound_uninit(&sound);
        mfSource.reset(); // only after the sound stopped reading from it
        loaded = false;
    }
};

// Reverb between the tracks and the output. Settings arrive as atomics from
// the GUI thread; the mixing thread applies them. Off effects are skipped entirely.
struct EffectsNode
{
    ma_node_base base{}; // first member: miniaudio treats a pointer to this struct as the node
    ma_uint32 channels = 2;
    Reverb reverb;
    std::atomic<float> reverbAmount{0};
    std::atomic<bool> mono{false};
    // Mixing-thread state.
    float appliedReverb = 0;

    static void process(ma_node *node, const float **framesIn, ma_uint32 *frameCountIn, float **framesOut,
                        ma_uint32 *frameCountOut)
    {
        auto *self = reinterpret_cast<EffectsNode *>(node);
        const ma_uint32 frames = std::min(*frameCountIn, *frameCountOut);
        const std::size_t samples = std::size_t(frames) * self->channels;
        float *out = framesOut[0];

        std::copy(framesIn[0], framesIn[0] + samples, out);

        const float amount = self->reverbAmount.load(std::memory_order_relaxed);
        if (amount != self->appliedReverb) {
            if (self->appliedReverb == 0)
                self->reverb.clear();
            self->reverb.setAmount(amount);
            self->appliedReverb = amount;
        }
        if (amount > 0 && self->channels == 2) // the reverb is stereo
            self->reverb.process(out, frames);

        // Mono: both channels get their average (after the reverb, so all of it is heard on each side).
        if (self->channels == 2 && self->mono.load(std::memory_order_relaxed)) {
            for (ma_uint32 i = 0; i < frames; ++i) {
                const float mid = 0.5f * (out[2 * i] + out[2 * i + 1]);
                out[2 * i] = out[2 * i + 1] = mid;
            }
        }

        *frameCountIn = frames;
        *frameCountOut = frames;
    }
};

ma_node_vtable kEffectsVtable{EffectsNode::process, nullptr, 1, 1, 0};

} // namespace

struct AudioEngine::Impl
{
    ma_context context{};
    bool contextReady = false;
    ma_engine engine{};
    bool engineReady = false;
    // Two slots so a crossfade can play the old track while the new one starts:
    // slots[current] is the track; the other one is empty or still fading out.
    Slot slots[2];
    int current = 0;
    // The other slot holds the next track, scheduled to start when this one ends (gapless).
    bool queued = false;
    EffectsNode effects;
    bool effectsReady = false;
    float speed = 1;
    float volume = 1;

    Slot &active() { return slots[current]; }
    const Slot &active() const { return slots[current]; }
    Slot &outgoing() { return slots[1 - current]; }

    // Opens the output (nullptr = system default) with the effects node; stop() undoes it.
    void start(ma_device_id *device);
    void stop();

    ma_uint32 sampleRate() const
    {
        ma_uint32 rate = 0;
        ma_sound_get_data_format(const_cast<ma_sound *>(&active().sound), nullptr, nullptr, &rate, nullptr, 0);
        return rate;
    }

    // Opens `file` into `slot` (paused at 0). WAV/FLAC/MP3 stream through miniaudio,
    // AAC/M4A through AacSource (Media Foundation / FFmpeg).
    bool open(Slot &slot, const std::filesystem::path &file)
    {
        // NO_SPATIALIZATION: plain stereo playback, no 3D panning/attenuation.
        ma_result result = MA_ERROR;
        if (AacSource::handles(file)) {
            slot.mfSource = AacSource::open(file);
            if (slot.mfSource)
                result = ma_sound_init_from_data_source(&engine, slot.mfSource->dataSource(),
                                                        MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &slot.sound);
        } else {
            // STREAM: decode incrementally on the resource manager thread instead of loading the whole file.
            constexpr ma_uint32 kFlags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
#ifdef _WIN32
            // Wide-char path keeps non-ASCII file names working on Windows (UTF-8 bytes on Linux).
            result = ma_sound_init_from_file_w(&engine, file.c_str(), kFlags, nullptr, nullptr, &slot.sound);
#else
            result = ma_sound_init_from_file(&engine, file.c_str(), kFlags, nullptr, nullptr, &slot.sound);
#endif
        }
        if (result != MA_SUCCESS) {
            std::fprintf(stderr, "AudioEngine: cannot load '%s': %s\n", file.string().c_str(),
                         ma_result_description(result));
            slot.mfSource.reset();
            return false;
        }
        slot.loaded = true;
        // Through the effects (if they could be set up) instead of straight to the output.
        if (effectsReady)
            ma_node_attach_output_bus(&slot.sound, 0, &effects, 0);
        ma_sound_set_pitch(&slot.sound, speed);
        return true;
    }

    void dropQueued()
    {
        if (queued)
            outgoing().unload();
        queued = false;
    }
};

AudioEngine::AudioEngine()
    : m_impl(std::make_unique<Impl>())
{
    // Own context so devices can be listed and the engine reopened on another one.
    m_impl->contextReady = ma_context_init(nullptr, 0, nullptr, &m_impl->context) == MA_SUCCESS;
    m_impl->start(nullptr);
}

void AudioEngine::Impl::start(ma_device_id *device)
{
    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.pContext = contextReady ? &context : nullptr;
    engineConfig.pPlaybackDeviceID = device;
    const ma_result result = ma_engine_init(&engineConfig, &engine);
    if (result != MA_SUCCESS) {
        std::fprintf(stderr, "AudioEngine: cannot open output device: %s\n", ma_result_description(result));
        return;
    }
    engineReady = true;
    ma_engine_set_volume(&engine, volume);

    // Effects node: allocate everything now, the mixing thread must not.
    effects.channels = ma_engine_get_channels(&engine);
    const ma_uint32 rate = ma_engine_get_sample_rate(&engine);
    effects.reverb.prepare(rate);
    ma_node_config config = ma_node_config_init();
    config.vtable = &kEffectsVtable;
    config.pInputChannels = &effects.channels;
    config.pOutputChannels = &effects.channels;
    if (ma_node_init(ma_engine_get_node_graph(&engine), &config, nullptr, &effects.base) == MA_SUCCESS) {
        ma_node_attach_output_bus(&effects.base, 0, ma_engine_get_endpoint(&engine), 0);
        effectsReady = true;
    } else {
        std::fprintf(stderr, "AudioEngine: effects unavailable\n");
    }
}

void AudioEngine::Impl::stop()
{
    slots[0].unload();
    slots[1].unload();
    queued = false;
    if (effectsReady)
        ma_node_uninit(&effects.base, nullptr);
    effectsReady = false;
    if (engineReady)
        ma_engine_uninit(&engine);
    engineReady = false;
}

AudioEngine::~AudioEngine()
{
    m_impl->stop();
    if (m_impl->contextReady)
        ma_context_uninit(&m_impl->context);
}

std::vector<std::string> AudioEngine::outputDevices() const
{
    std::vector<std::string> names;
    ma_device_info *devices = nullptr;
    ma_uint32 count = 0;
    if (m_impl->contextReady
        && ma_context_get_devices(&m_impl->context, &devices, &count, nullptr, nullptr) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i)
            names.emplace_back(devices[i].name);
    }
    return names;
}

void AudioEngine::setOutputDevice(const std::string &name)
{
    // Copy the id before stopping: the device list belongs to the context.
    ma_device_id id{};
    bool found = false;
    ma_device_info *devices = nullptr;
    ma_uint32 count = 0;
    if (!name.empty() && m_impl->contextReady
        && ma_context_get_devices(&m_impl->context, &devices, &count, nullptr, nullptr) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count && !found; ++i) {
            if (name == devices[i].name) {
                id = devices[i].id;
                found = true;
            }
        }
    }
    m_impl->stop();
    m_impl->start(found ? &id : nullptr);
}

bool AudioEngine::isReady() const
{
    return m_impl->engineReady;
}

bool AudioEngine::load(const std::filesystem::path &file, int crossfadeMs)
{
    if (!m_impl->engineReady)
        return false;

    // A track still fading out from an earlier crossfade (or queued for gapless) is cut now.
    m_impl->outgoing().unload();
    m_impl->queued = false;
    if (crossfadeMs > 0 && isPlaying()) {
        // The current track becomes the outgoing one: it fades to silence and stops by itself.
        ma_sound_stop_with_fade_in_milliseconds(&m_impl->active().sound, static_cast<ma_uint64>(crossfadeMs));
        m_impl->current = 1 - m_impl->current;
    } else {
        m_impl->active().unload();
        crossfadeMs = 0;
    }

    Slot &slot = m_impl->active();
    if (!m_impl->open(slot, file))
        return false;
    if (crossfadeMs > 0)
        ma_sound_set_fade_in_milliseconds(&slot.sound, 0.0f, 1.0f, static_cast<ma_uint64>(crossfadeMs));
    return true;
}

void AudioEngine::update()
{
    // Free the outgoing track once its fade-out has stopped it (a queued track is not outgoing).
    Slot &outgoing = m_impl->outgoing();
    if (!m_impl->queued && outgoing.loaded && !ma_sound_is_playing(&outgoing.sound))
        outgoing.unload();
}

bool AudioEngine::queueNext(const std::filesystem::path &file, float gain)
{
    if (m_impl->queued || !isPlaying())
        return false;
    Slot &current = m_impl->active();
    const ma_uint32 rate = m_impl->sampleRate();
    ma_uint64 length = 0;
    if (rate == 0 || ma_sound_get_length_in_pcm_frames(&current.sound, &length) != MA_SUCCESS || length == 0)
        return false; // unknown length: no exact end to schedule against

    Slot &next = m_impl->outgoing();
    next.unload(); // a finished crossfade-out not yet freed by update()
    if (!m_impl->open(next, file))
        return false;
    ma_sound_set_volume(&next.sound, gain);

    // The engine clock and the track's cursor both advance on the mixing thread; read them
    // between two mixes (same clock before and after) so they describe the same moment.
    const ma_uint32 engineRate = ma_engine_get_sample_rate(&m_impl->engine);
    ma_uint64 now = 0;
    ma_uint64 cursor = 0;
    for (int attempt = 0; attempt < 5; ++attempt) {
        now = ma_engine_get_time_in_pcm_frames(&m_impl->engine);
        ma_sound_get_cursor_in_pcm_frames(&current.sound, &cursor);
        if (ma_engine_get_time_in_pcm_frames(&m_impl->engine) == now)
            break;
    }
    const ma_uint64 remaining = length > cursor ? length - cursor : 0;
    // Remaining frames are in the track's rate; the engine clock runs at the device rate,
    // and a slowed track takes 1/speed as long to play them.
    const auto engineFrames = static_cast<ma_uint64>(std::llround(double(remaining) * engineRate / rate / m_impl->speed));
    ma_sound_set_start_time_in_pcm_frames(&next.sound, now + engineFrames);
    ma_sound_start(&next.sound);
    m_impl->queued = true;
    return true;
}

bool AudioEngine::hasQueued() const
{
    return m_impl->queued;
}

void AudioEngine::cancelQueued()
{
    m_impl->dropQueued();
}

bool AudioEngine::promoteQueued()
{
    if (!m_impl->queued)
        return false;
    m_impl->active().unload();
    m_impl->current = 1 - m_impl->current;
    m_impl->queued = false;
    return true;
}

void AudioEngine::unload()
{
    m_impl->slots[0].unload();
    m_impl->slots[1].unload();
    m_impl->queued = false;
}

bool AudioEngine::hasTrack() const
{
    return m_impl->active().loaded;
}

void AudioEngine::play()
{
    // Windows can stop the output device while idle (sleeping headphones, display off, device
    // change); the engine then stays stopped and sounds play into nothing.
    if (ma_device *device = ma_engine_get_device(&m_impl->engine);
        m_impl->engineReady && device && ma_device_get_state(device) != ma_device_state_started) {
        if (const ma_result result = ma_engine_start(&m_impl->engine); result != MA_SUCCESS)
            std::fprintf(stderr, "AudioEngine: cannot restart output device: %s\n", ma_result_description(result));
    }
    if (m_impl->active().loaded)
        ma_sound_start(&m_impl->active().sound);
}

void AudioEngine::pause()
{
    if (m_impl->active().loaded)
        ma_sound_stop(&m_impl->active().sound);
    // Pausing during a crossfade silences the fading track too, and drops a queued next track
    // (its start time assumed uninterrupted playback; it is queued again after resuming).
    m_impl->outgoing().unload();
    m_impl->queued = false;
}

bool AudioEngine::isPlaying() const
{
    return m_impl->active().loaded && ma_sound_is_playing(&m_impl->active().sound);
}

bool AudioEngine::atEnd() const
{
    return m_impl->active().loaded && ma_sound_at_end(&m_impl->active().sound);
}

void AudioEngine::seek(std::int64_t positionMs)
{
    const ma_uint32 rate = m_impl->sampleRate();
    if (!m_impl->active().loaded || rate == 0)
        return;
    // A queued track was timed for the old position.
    m_impl->dropQueued();
    const auto frame = static_cast<ma_uint64>(std::max<std::int64_t>(positionMs, 0)) * rate / 1000;
    ma_sound_seek_to_pcm_frame(&m_impl->active().sound, frame);
}

std::int64_t AudioEngine::positionMs() const
{
    const ma_uint32 rate = m_impl->sampleRate();
    ma_uint64 cursor = 0;
    if (!m_impl->active().loaded || rate == 0
        || ma_sound_get_cursor_in_pcm_frames(&m_impl->active().sound, &cursor) != MA_SUCCESS)
        return 0;
    return static_cast<std::int64_t>(cursor * 1000 / rate);
}

std::int64_t AudioEngine::durationMs() const
{
    const ma_uint32 rate = m_impl->sampleRate();
    ma_uint64 length = 0;
    if (!m_impl->active().loaded || rate == 0
        || ma_sound_get_length_in_pcm_frames(&m_impl->active().sound, &length) != MA_SUCCESS)
        return 0;
    return static_cast<std::int64_t>(length * 1000 / rate);
}

void AudioEngine::setVolume(float volume)
{
    m_impl->volume = std::clamp(volume, 0.0f, 1.0f);
    if (m_impl->engineReady)
        ma_engine_set_volume(&m_impl->engine, m_impl->volume);
}

void AudioEngine::setTrackGain(float gain)
{
    if (m_impl->active().loaded)
        ma_sound_set_volume(&m_impl->active().sound, gain);
}

void AudioEngine::setSpeed(float speed)
{
    speed = std::clamp(speed, 0.5f, 1.5f);
    if (speed == m_impl->speed)
        return;
    m_impl->speed = speed;
    // A queued track was timed for the old speed.
    m_impl->dropQueued();
    for (Slot &slot : m_impl->slots) {
        if (slot.loaded)
            ma_sound_set_pitch(&slot.sound, speed);
    }
}

void AudioEngine::setMono(bool mono)
{
    m_impl->effects.mono.store(mono, std::memory_order_relaxed);
}

void AudioEngine::setReverb(float amount)
{
    m_impl->effects.reverbAmount.store(std::clamp(amount, 0.0f, 1.0f), std::memory_order_relaxed);
}

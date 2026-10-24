#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Plays one track at a time on an output device (miniaudio, WASAPI), the system default unless set.
// WAV/FLAC/MP3 decode in miniaudio; M4A/AAC through AacSource (Media Foundation on Windows, FFmpeg on Linux).
// Call every method from one thread (the GUI thread). miniaudio decodes on its
// own job thread and mixes on the device thread; the calls used here are safe
// against both.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;

    // False if no output device could be opened; every other call is then a no-op.
    bool isReady() const;

    // Replaces the current track. The new track starts paused at 0.
    // crossfadeMs > 0 while playing: the old track fades out over that time while the new
    // one fades in once play() is called.
    bool load(const std::filesystem::path &file, int crossfadeMs = 0);
    // Frees a track whose crossfade-out has finished. Call regularly (the player's poll).
    void update();

    // Gapless playback: loads `file` and schedules it to start on the exact frame the
    // current track ends. Only while playing, with a known track length and no crossfade
    // running. Cancelled by load(), pause(), seek() and cancelQueued().
    bool queueNext(const std::filesystem::path &file, float gain = 1.0f);
    bool hasQueued() const;
    void cancelQueued();
    // The current track has ended and the queued one took over: makes it the current track.
    // False if nothing was queued.
    bool promoteQueued();
    void unload();
    bool hasTrack() const;

    void play();
    void pause();
    bool isPlaying() const;
    // True once playback has reached the end; play() then restarts from 0.
    bool atEnd() const;

    void seek(std::int64_t positionMs);
    std::int64_t positionMs() const;
    std::int64_t durationMs() const; // 0 if the decoder cannot tell

    // Master output volume, linear 0..1. Per-track gain (ReplayGain) is separate.
    void setVolume(float volume);
    // ReplayGain of the current track, linear (1 = unchanged). A new track starts at 1.
    void setTrackGain(float gain);

    // Effects, applied to everything that plays. Speed resamples (1 = normal, 0.8 = slower and
    // lower, like a slowed-down record); reverb 0..1.
    void setSpeed(float speed);
    void setReverb(float amount);
    // Plays both channels as their average (one earbud, uneven hearing).
    void setMono(bool mono);

    // Output device names, for setOutputDevice().
    std::vector<std::string> outputDevices() const;
    // Reopens the output on the device with this name (UTF-8); empty or unknown = system default.
    // Unloads everything: the caller reloads its track.
    void setOutputDevice(const std::string &name);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

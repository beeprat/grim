#pragma once

#include <miniaudio.h>

#include <filesystem>
#include <memory>

// Decodes AAC / M4A with Windows Media Foundation into float PCM, exposed as a
// miniaudio data source. miniaudio's built-in decoders cover WAV, FLAC and MP3 only.
//
// shortcut: decoding runs on whatever thread reads the data source (the mixing
// thread when played through ma_sound), not miniaudio's job thread. Fine for local
// files; wrap it in a ring buffer filled by a worker if playback ever glitches.
class MfAudioSource
{
public:
    // True for extensions this source should be used for.
    static bool handles(const std::filesystem::path &file);
    // nullptr if Media Foundation cannot open or decode the file.
    static std::unique_ptr<MfAudioSource> open(const std::filesystem::path &file);

    ~MfAudioSource();
    MfAudioSource(const MfAudioSource &) = delete;
    MfAudioSource &operator=(const MfAudioSource &) = delete;

    ma_data_source *dataSource() const;

    struct State;

private:
    explicit MfAudioSource(std::unique_ptr<State> state);

    std::unique_ptr<State> m_state;
};

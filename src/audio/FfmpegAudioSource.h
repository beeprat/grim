#pragma once

#include <miniaudio.h>

#include <filesystem>
#include <memory>

// Linux counterpart of MfAudioSource: decodes AAC / M4A with FFmpeg's libavformat/libavcodec into
// float PCM, exposed as a miniaudio data source. Same interface, picked through AacSource.h.
//
// shortcut: decoding runs on whatever thread reads the data source (the mixing thread when played
// through ma_sound), like MfAudioSource. Fine for local files; add a worker + ring buffer if it glitches.
class FfmpegAudioSource
{
public:
    // True for extensions this source should be used for.
    static bool handles(const std::filesystem::path &file);
    // nullptr if FFmpeg cannot open or decode the file.
    static std::unique_ptr<FfmpegAudioSource> open(const std::filesystem::path &file);

    ~FfmpegAudioSource();
    FfmpegAudioSource(const FfmpegAudioSource &) = delete;
    FfmpegAudioSource &operator=(const FfmpegAudioSource &) = delete;

    ma_data_source *dataSource() const;

    struct State;

private:
    explicit FfmpegAudioSource(std::unique_ptr<State> state);

    std::unique_ptr<State> m_state;
};

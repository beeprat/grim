#pragma once

#include <cstddef>
#include <memory>

namespace soundtouch {
class SoundTouch;
}

// Changes pitch without changing tempo (SoundTouch's time-stretch + resample), on blocks of
// interleaved float samples. Output lags input by SoundTouch's processing latency (~0.1 s):
// the first blocks after a change of state come out partly silent.
class PitchShifter
{
public:
    PitchShifter();
    ~PitchShifter();

    // Allocates; call before process(), not on the audio thread.
    void prepare(unsigned sampleRate, unsigned channels);
    void setSemitones(float semitones);
    // Drops buffered audio (after the effect was off, or a seek).
    void clear();

    // Writes exactly frameCount output frames; silence where SoundTouch has none yet.
    void process(const float *input, float *output, std::size_t frameCount);

private:
    std::unique_ptr<soundtouch::SoundTouch> m_soundTouch;
    unsigned m_channels = 0;
};

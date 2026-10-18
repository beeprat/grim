#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Stereo room reverb after Jezar's public-domain Freeverb: 8 parallel damped comb filters
// and 4 series all-pass filters per channel, the right channel slightly detuned for width.
// prepare() allocates; process() does not, so it can run on the audio thread.
class Reverb
{
public:
    // Sizes the delay lines for this sample rate and clears them.
    void prepare(unsigned sampleRate);
    // Silences the tail (e.g. after the effect was off).
    void clear();

    // amount 0 = dry signal only, 1 = strongest reverb.
    void setAmount(float amount);

    // In place, interleaved stereo.
    void process(float *frames, std::size_t frameCount);

private:
    struct Comb
    {
        std::vector<float> buffer;
        std::size_t index = 0;
        float store = 0;
        float process(float input, float feedback, float damp);
    };
    struct AllPass
    {
        std::vector<float> buffer;
        std::size_t index = 0;
        float process(float input);
    };
    struct Channel
    {
        std::array<Comb, 8> combs;
        std::array<AllPass, 4> allPasses;
        float process(float input, float feedback, float damp);
    };

    Channel m_left;
    Channel m_right;
    float m_wet = 0;
    float m_dry = 1;
};

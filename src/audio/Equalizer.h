#pragma once

#include <array>
#include <vector>

// 16-band graphic equalizer: one peaking filter per band (RBJ biquads), bands 0.6 octave apart
// from 31 Hz to 16 kHz. prepare() allocates; process() does not (it runs on the mixing thread).
class Equalizer
{
public:
    static constexpr int kBands = 16;
    static constexpr float kMaxGainDb = 12.0f;

    // Band centre frequency in Hz.
    static float frequency(int band);

    void prepare(unsigned sampleRate, unsigned channels);
    // Gains in dB, clamped to ±kMaxGainDb. While any band is boosted, peaks above 0.8 are
    // softly limited so they do not clip.
    void setGains(const std::array<float, kBands> &gainsDb);
    void clear();
    // Interleaved frames, in place.
    void process(float *samples, unsigned frames);

private:
    struct Coefficients
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    struct State
    {
        float z1 = 0, z2 = 0;
    };

    unsigned m_rate = 48000;
    unsigned m_channels = 2;
    bool m_boosted = false;
    std::array<Coefficients, kBands> m_coefficients{};
    std::array<bool, kBands> m_active{};
    std::vector<State> m_state; // band * channels + channel
};

#include "audio/Equalizer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr float kLowest = 31.25f;
constexpr float kOctavesApart = 0.6f; // 15 steps of 0.6 octave: 31.25 Hz → 16 kHz
// Bandwidth matching the spacing: neighbouring bands meet about halfway down.
const float kQ = std::sqrt(std::exp2(kOctavesApart)) / (std::exp2(kOctavesApart) - 1);

} // namespace

float Equalizer::frequency(int band)
{
    return kLowest * std::exp2(kOctavesApart * float(band));
}

void Equalizer::prepare(unsigned sampleRate, unsigned channels)
{
    m_rate = sampleRate;
    m_channels = channels;
    m_state.assign(std::size_t(kBands) * channels, State{});
}

void Equalizer::clear()
{
    std::fill(m_state.begin(), m_state.end(), State{});
}

void Equalizer::setGains(const std::array<float, kBands> &gainsDb)
{
    float maxBoost = 0;
    for (int band = 0; band < kBands; ++band) {
        const float gain = std::clamp(gainsDb[band], -kMaxGainDb, kMaxGainDb);
        maxBoost = std::max(maxBoost, gain);
        m_active[band] = gain != 0;
        // Bands above Nyquist (low sample rates) stay off.
        const float f = frequency(band);
        if (!m_active[band] || f >= 0.45f * float(m_rate)) {
            m_active[band] = false;
            continue;
        }
        const float a = std::pow(10.0f, gain / 40);
        const float w = 2 * std::numbers::pi_v<float> * f / float(m_rate);
        const float alpha = std::sin(w) / (2 * kQ);
        const float cosw = std::cos(w);
        const float a0 = 1 + alpha / a;
        m_coefficients[band] = {(1 + alpha * a) / a0, -2 * cosw / a0, (1 - alpha * a) / a0, -2 * cosw / a0,
                                (1 - alpha / a) / a0};
    }
    m_boosted = maxBoost > 0;
}

void Equalizer::process(float *samples, unsigned frames)
{
    for (int band = 0; band < kBands; ++band) {
        if (!m_active[band])
            continue;
        const Coefficients c = m_coefficients[band];
        for (unsigned channel = 0; channel < m_channels; ++channel) {
            State &s = m_state[std::size_t(band) * m_channels + channel];
            float *x = samples + channel;
            for (unsigned i = 0; i < frames; ++i, x += m_channels) {
                // Transposed direct form II.
                const float in = *x;
                const float out = c.b0 * in + s.z1;
                s.z1 = c.b1 * in - c.a1 * out + s.z2;
                s.z2 = c.b2 * in - c.a2 * out;
                *x = out;
            }
        }
    }
    // Boosts keep their loudness (no lowered preamp); only peaks they push near full scale are
    // rounded off, instead of clipping hard at the output.
    if (m_boosted) {
        constexpr float kKnee = 0.8f;
        const std::size_t count = std::size_t(frames) * m_channels;
        for (std::size_t i = 0; i < count; ++i) {
            const float x = samples[i];
            const float magnitude = std::fabs(x);
            if (magnitude > kKnee)
                samples[i] = std::copysign(kKnee + (1 - kKnee) * std::tanh((magnitude - kKnee) / (1 - kKnee)), x);
        }
    }
}

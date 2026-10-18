#include "audio/Reverb.h"

#include <algorithm>
#include <cmath>

namespace {

// Freeverb's tunings, in samples at 44.1 kHz.
constexpr std::array<int, 8> kCombLengths{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<int, 4> kAllPassLengths{556, 441, 341, 225};
constexpr int kStereoSpread = 23;

constexpr float kFeedback = 0.84f; // room size
constexpr float kDamp = 0.2f;      // high frequencies fade faster than lows
constexpr float kInputGain = 0.015f;
constexpr float kWetScale = 3.0f;

// Very small values in the feedback loops would otherwise turn denormal and slow the CPU down.
float flushDenormal(float value)
{
    return std::fabs(value) < 1e-20f ? 0.0f : value;
}

} // namespace

float Reverb::Comb::process(float input, float feedback, float damp)
{
    const float output = buffer[index];
    store = flushDenormal(output * (1 - damp) + store * damp);
    buffer[index] = input + store * feedback;
    if (++index == buffer.size())
        index = 0;
    return output;
}

float Reverb::AllPass::process(float input)
{
    const float buffered = flushDenormal(buffer[index]);
    buffer[index] = input + buffered * 0.5f;
    if (++index == buffer.size())
        index = 0;
    return buffered - input;
}

float Reverb::Channel::process(float input, float feedback, float damp)
{
    float sum = 0;
    for (Comb &comb : combs)
        sum += comb.process(input, feedback, damp);
    for (AllPass &allPass : allPasses)
        sum = allPass.process(sum);
    return sum;
}

void Reverb::prepare(unsigned sampleRate)
{
    const double scale = sampleRate / 44100.0;
    const auto size = [scale](int length, int spread) {
        return static_cast<std::size_t>(std::max(1.0, std::round((length + spread) * scale)));
    };
    for (std::size_t i = 0; i < kCombLengths.size(); ++i) {
        m_left.combs[i].buffer.assign(size(kCombLengths[i], 0), 0.0f);
        m_right.combs[i].buffer.assign(size(kCombLengths[i], kStereoSpread), 0.0f);
    }
    for (std::size_t i = 0; i < kAllPassLengths.size(); ++i) {
        m_left.allPasses[i].buffer.assign(size(kAllPassLengths[i], 0), 0.0f);
        m_right.allPasses[i].buffer.assign(size(kAllPassLengths[i], kStereoSpread), 0.0f);
    }
    clear();
}

void Reverb::clear()
{
    for (Channel *channel : {&m_left, &m_right}) {
        for (Comb &comb : channel->combs) {
            std::fill(comb.buffer.begin(), comb.buffer.end(), 0.0f);
            comb.index = 0;
            comb.store = 0;
        }
        for (AllPass &allPass : channel->allPasses) {
            std::fill(allPass.buffer.begin(), allPass.buffer.end(), 0.0f);
            allPass.index = 0;
        }
    }
}

void Reverb::setAmount(float amount)
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    // A little less dry signal as the room grows, so loudness stays about even.
    m_wet = amount * kWetScale * 0.5f;
    m_dry = 1.0f - amount * 0.3f;
}

void Reverb::process(float *frames, std::size_t frameCount)
{
    if (m_left.combs[0].buffer.empty())
        return; // not prepared
    for (std::size_t i = 0; i < frameCount; ++i) {
        float &left = frames[2 * i];
        float &right = frames[2 * i + 1];
        // Both channels feed one mono input, as in Freeverb; the detuned right side adds width.
        const float input = (left + right) * kInputGain;
        const float wetLeft = m_left.process(input, kFeedback, kDamp);
        const float wetRight = m_right.process(input, kFeedback, kDamp);
        left = left * m_dry + wetLeft * m_wet;
        right = right * m_dry + wetRight * m_wet;
    }
}

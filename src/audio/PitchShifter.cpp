#include "audio/PitchShifter.h"

#include <SoundTouch.h>

#include <algorithm>

PitchShifter::PitchShifter() = default;
PitchShifter::~PitchShifter() = default;

void PitchShifter::prepare(unsigned sampleRate, unsigned channels)
{
    m_soundTouch = std::make_unique<soundtouch::SoundTouch>();
    m_channels = channels;
    m_soundTouch->setSampleRate(sampleRate);
    m_soundTouch->setChannels(channels);
    // Quality over CPU: full search for the best overlap, anti-alias filter on.
    m_soundTouch->setSetting(SETTING_USE_QUICKSEEK, 0);
    m_soundTouch->setSetting(SETTING_USE_AA_FILTER, 1);
}

void PitchShifter::setSemitones(float semitones)
{
    if (m_soundTouch)
        m_soundTouch->setPitchSemiTones(semitones);
}

void PitchShifter::clear()
{
    if (m_soundTouch)
        m_soundTouch->clear();
}

void PitchShifter::process(const float *input, float *output, std::size_t frameCount)
{
    if (!m_soundTouch) {
        std::copy(input, input + frameCount * m_channels, output);
        return;
    }
    m_soundTouch->putSamples(input, static_cast<unsigned>(frameCount));
    const unsigned received = m_soundTouch->receiveSamples(output, static_cast<unsigned>(frameCount));
    std::fill(output + std::size_t(received) * m_channels, output + frameCount * m_channels, 0.0f);
}

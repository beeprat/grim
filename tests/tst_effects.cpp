#include "audio/PitchShifter.h"
#include "audio/Reverb.h"

#include <QTest>

#include <cmath>
#include <numbers>
#include <vector>

namespace {

constexpr unsigned kRate = 44100;

// Interleaved stereo sine.
std::vector<float> sine(double frequency, std::size_t frames)
{
    std::vector<float> samples(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
        samples[2 * i] = samples[2 * i + 1] = float(0.5 * std::sin(2 * std::numbers::pi * frequency * i / kRate));
    return samples;
}

// Frequency of the left channel from its rising zero crossings.
double frequencyOf(const std::vector<float> &samples, std::size_t fromFrame)
{
    int crossings = 0;
    std::size_t first = 0;
    std::size_t last = 0;
    for (std::size_t i = fromFrame + 1; i < samples.size() / 2; ++i) {
        if (samples[2 * (i - 1)] < 0 && samples[2 * i] >= 0) {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    }
    return crossings > 1 ? (crossings - 1) * double(kRate) / double(last - first) : 0;
}

} // namespace

class TestEffects : public QObject
{
    Q_OBJECT

private slots:
    void reverbAtZeroLeavesTheSignalAlone()
    {
        Reverb reverb;
        reverb.prepare(kRate);
        reverb.setAmount(0);
        std::vector<float> samples = sine(440, 4096);
        const std::vector<float> original = samples;
        reverb.process(samples.data(), 4096);
        QCOMPARE(samples, original);
    }

    void reverbLeavesATailAfterAClick()
    {
        Reverb reverb;
        reverb.prepare(kRate);
        reverb.setAmount(0.5f);
        std::vector<float> samples(kRate * 2, 0.0f); // one second of stereo
        samples[0] = samples[1] = 1.0f;
        reverb.process(samples.data(), kRate);

        // Echoes keep sounding well after the click, on both sides, and stay bounded.
        double tail = 0;
        for (std::size_t i = kRate / 2 * 2; i < samples.size(); ++i) {
            QVERIFY(std::isfinite(samples[i]));
            QVERIFY(std::fabs(samples[i]) < 1.0f);
            tail += std::fabs(samples[i]);
        }
        QVERIFY(tail > 0.01);
        QVERIFY(samples[2 * 5000] != samples[2 * 5000 + 1]); // left and right differ: stereo width
    }

    void pitchShiftKeepsTempoAndMovesTheKey_data()
    {
        QTest::addColumn<float>("semitones");
        QTest::addColumn<double>("expected");
        QTest::newRow("octave up") << 12.0f << 880.0;
        QTest::newRow("fifth down") << -7.0f << 440.0 * std::pow(2.0, -7.0 / 12);
    }

    void pitchShiftKeepsTempoAndMovesTheKey()
    {
        QFETCH(float, semitones);
        QFETCH(double, expected);

        PitchShifter shifter;
        shifter.prepare(kRate, 2);
        shifter.setSemitones(semitones);

        // Two seconds in blocks, like the mixer: every block gives as many frames as it takes.
        constexpr std::size_t kBlock = 480;
        const std::vector<float> input = sine(440, kRate * 2);
        std::vector<float> output(input.size());
        for (std::size_t frame = 0; frame + kBlock <= kRate * 2; frame += kBlock)
            shifter.process(input.data() + 2 * frame, output.data() + 2 * frame, kBlock);

        // Skip the start (SoundTouch's latency outputs silence first).
        QVERIFY(qAbs(frequencyOf(output, kRate / 2) - expected) < expected * 0.02);
    }
};

QTEST_GUILESS_MAIN(TestEffects)
#include "tst_effects.moc"

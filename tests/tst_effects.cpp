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
};

QTEST_GUILESS_MAIN(TestEffects)
#include "tst_effects.moc"

#include "audio/Equalizer.h"

#include <QTest>

#include <cmath>
#include <numbers>
#include <vector>

namespace {

// Peak of a stereo sine at the given frequency after the equalizer (after it settles).
float peakAfter(Equalizer &eq, float frequency, float amplitude)
{
    constexpr unsigned kRate = 48000;
    constexpr unsigned kFrames = kRate / 2;
    std::vector<float> samples(kFrames * 2);
    for (unsigned i = 0; i < kFrames; ++i)
        samples[2 * i] = samples[2 * i + 1] = amplitude * std::sin(2 * std::numbers::pi_v<float> * frequency * float(i) / kRate);
    eq.process(samples.data(), kFrames);
    float peak = 0;
    for (unsigned i = kFrames / 2 * 2; i < samples.size(); ++i)
        peak = std::max(peak, std::fabs(samples[i]));
    return peak;
}

} // namespace

class TestEqualizer : public QObject
{
    Q_OBJECT

private slots:
    void flatChangesNothing()
    {
        Equalizer eq;
        eq.prepare(48000, 2);
        eq.setGains({});
        QVERIFY(std::fabs(peakAfter(eq, 1000, 0.5f) - 0.5f) < 0.01f);
    }

    void boostRaisesItsBandWithoutLoweringOthers()
    {
        Equalizer eq;
        eq.prepare(48000, 2);
        std::array<float, Equalizer::kBands> gains{};
        gains[8] = 6; // 873 Hz
        eq.setGains(gains);
        QVERIFY(peakAfter(eq, Equalizer::frequency(8), 0.2f) > 0.38f); // about +6 dB
        eq.clear();
        QVERIFY(std::fabs(peakAfter(eq, 100, 0.2f) - 0.2f) < 0.02f); // far from the band: same level
    }

    void boostedPeaksStayBelowFullScale()
    {
        Equalizer eq;
        eq.prepare(48000, 2);
        std::array<float, Equalizer::kBands> gains{};
        gains[8] = 12;
        eq.setGains(gains);
        QVERIFY(peakAfter(eq, Equalizer::frequency(8), 0.9f) <= 1.0f);
    }
};

QTEST_GUILESS_MAIN(TestEqualizer)
#include "tst_equalizer.moc"

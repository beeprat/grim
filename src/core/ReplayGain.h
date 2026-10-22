#pragma once

#include <QString>

#include <optional>

// Per-song volume adjustment in dB (right-click → "Volume adjustment…"). The file's ReplayGain tags
// (REPLAYGAIN_TRACK_GAIN etc., written by foobar2000, beets, Mp3tag...) are only shown there as a hint.
namespace ReplayGain {

// The user's own adjustments are limited to this range, in dB.
constexpr double kMaxAdjustDb = 20.0;

struct Info
{
    std::optional<double> trackGainDb;
    std::optional<double> trackPeak;
    std::optional<double> albumGainDb;
    std::optional<double> albumPeak;
};

Info read(const QString &audioPath);
// "-6.48 dB", "+1.2 dB", "-6.48": the number; nothing for anything else.
std::optional<double> parseNumber(const QString &text);
// dB to a linear volume.
float fromDb(double db);

} // namespace ReplayGain

#pragma once

#include <QString>

#include <optional>

// ReplayGain: the loudness correction stored in a file's tags (REPLAYGAIN_TRACK_GAIN etc., written by
// foobar2000, beets, Mp3tag...). The player applies it as a per-track volume; nothing is scanned here.
namespace ReplayGain {

// Settings → Playback. A song's own dB value (right-click → "Volume adjustment…") replaces the tags in every mode.
enum Mode { Off = 0, Track = 1, Album = 2 };

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
// Linear volume for `mode` (Off/Track/Album). Album falls back to the track gain and the other way round;
// the peak keeps a boost from clipping. 1 without tags.
float factor(const Info &info, int mode);
// dB to a linear volume.
float fromDb(double db);

} // namespace ReplayGain

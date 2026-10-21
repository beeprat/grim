#include "core/ReplayGain.h"

#include <fileref.h>
#include <tpropertymap.h>

#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace ReplayGain {

namespace {

std::optional<double> number(const TagLib::PropertyMap &tags, const char *key)
{
    const auto found = tags.find(key);
    if (found == tags.end() || found->second.isEmpty())
        return std::nullopt;
    return parseNumber(QString::fromStdWString(found->second.front().toWString()));
}

} // namespace

Info read(const QString &audioPath)
{
    const TagLib::FileRef file(QFileInfo(audioPath).filesystemFilePath().c_str(), false);
    if (file.isNull())
        return {};
    const TagLib::PropertyMap tags = file.properties();
    return {number(tags, "REPLAYGAIN_TRACK_GAIN"), number(tags, "REPLAYGAIN_TRACK_PEAK"),
            number(tags, "REPLAYGAIN_ALBUM_GAIN"), number(tags, "REPLAYGAIN_ALBUM_PEAK")};
}

std::optional<double> parseNumber(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^\s*([+-]?\d+(?:[.,]\d+)?)\s*(?:dB)?\s*$)"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch())
        return std::nullopt;
    return match.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
}

float factor(const Info &info, int mode)
{
    if (mode != Track && mode != Album)
        return 1.0f;
    const bool album = mode == Album ? info.albumGainDb.has_value() : !info.trackGainDb.has_value();
    const std::optional<double> gainDb = album ? info.albumGainDb : info.trackGainDb;
    if (!gainDb)
        return 1.0f;
    double gain = std::pow(10.0, *gainDb / 20.0);
    if (const std::optional<double> peak = album ? info.albumPeak : info.trackPeak; peak && *peak > 0)
        gain = std::min(gain, 1.0 / *peak);
    return static_cast<float>(gain);
}

} // namespace ReplayGain

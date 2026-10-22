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

float fromDb(double db)
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

} // namespace ReplayGain

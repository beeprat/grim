#include "core/Lyrics.h"

#include <fileref.h>
#include <tpropertymap.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace Lyrics {

QString read(const QString &audioPath)
{
    const std::filesystem::path path = QFileInfo(audioPath).filesystemFilePath();
    const TagLib::FileRef file(path.c_str(), false);
    if (!file.isNull()) {
        const TagLib::PropertyMap tags = file.properties();
        for (const char *key : {"LYRICS", "UNSYNCEDLYRICS"}) {
            const auto found = tags.find(key);
            if (found != tags.end() && !found->second.isEmpty()) {
                const QString text = QString::fromStdWString(found->second.toString("\n").toWString()).trimmed();
                if (!text.isEmpty())
                    return text;
            }
        }
    }

    const QFileInfo info(audioPath);
    QFile lrc(info.dir().filePath(info.completeBaseName() + u".lrc"_s));
    if (lrc.open(QIODevice::ReadOnly)) {
        const QByteArray data = lrc.readAll();
        // Most .lrc files are UTF-8; fall back to Latin-1 for old ones.
        QStringDecoder utf8(QStringDecoder::Utf8);
        QString text = utf8(data);
        if (utf8.hasError())
            text = QString::fromLatin1(data);
        return text.trimmed();
    }
    return {};
}

QList<Line> parse(const QString &text)
{
    static const QRegularExpression timeTag(uR"(\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\])"_s);
    static const QRegularExpression offsetTag(uR"(^\s*\[offset:\s*([+-]?\d+)\s*\])"_s,
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression metadataTag(uR"(^\s*\[[a-zA-Z#]+:.*\]\s*$)"_s);
    static const QRegularExpression wordTimings(uR"(<\d{1,3}:\d{1,2}(?:[.:]\d{1,3})?>)"_s);

    qint64 offsetMs = 0;
    QList<Line> synced;
    QList<Line> plain;
    const QStringList rows = text.split(QRegularExpression(u"\r\n|\r|\n"_s));
    for (const QString &row : rows) {
        if (const auto offset = offsetTag.match(row); offset.hasMatch()) {
            offsetMs = offset.captured(1).toLongLong();
            continue;
        }

        QList<qint64> times;
        qsizetype textStart = 0;
        auto it = timeTag.globalMatch(row);
        while (it.hasNext()) {
            const auto match = it.next();
            if (match.capturedStart() != textStart)
                break; // timestamps only count at the start of the row
            const QString fraction = match.captured(3);
            // ".5" = 500 ms, ".45" = 450 ms, ".456" = 456 ms.
            const qint64 fractionMs = fraction.isEmpty() ? 0 : (fraction + u"00"_s).left(3).toLongLong();
            times.append(match.captured(1).toLongLong() * 60'000 + match.captured(2).toLongLong() * 1000
                         + fractionMs);
            textStart = match.capturedEnd();
        }

        if (times.isEmpty()) {
            if (!metadataTag.match(row).hasMatch())
                plain.append({-1, row.trimmed()});
            continue;
        }
        const QString lineText = row.mid(textStart).remove(wordTimings).trimmed();
        for (const qint64 time : times)
            synced.append({std::max<qint64>(0, time - offsetMs), lineText});
    }

    if (synced.isEmpty()) {
        // Unsynced: keep the text, without leading/trailing blank rows.
        while (!plain.isEmpty() && plain.first().text.isEmpty())
            plain.removeFirst();
        while (!plain.isEmpty() && plain.last().text.isEmpty())
            plain.removeLast();
        return plain;
    }
    std::stable_sort(synced.begin(), synced.end(), [](const Line &a, const Line &b) { return a.timeMs < b.timeMs; });
    return synced;
}

int lineAt(const QList<Line> &lines, qint64 positionMs)
{
    const auto after = std::upper_bound(lines.cbegin(), lines.cend(), positionMs,
                                        [](qint64 position, const Line &line) { return position < line.timeMs; });
    return static_cast<int>(after - lines.cbegin()) - 1;
}

} // namespace Lyrics

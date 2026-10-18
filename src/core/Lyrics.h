#pragma once

#include <QList>
#include <QString>

namespace Lyrics {

struct Line
{
    qint64 timeMs = -1; // -1 for unsynced lyrics
    QString text;
};

// Lyrics for an audio file: the LYRICS tag (ID3 USLT, MP4 ©lyr, Vorbis LYRICS),
// or else a sidecar .lrc with the same base name. Empty if none.
QString read(const QString &audioPath);

// Parses LRC ("[01:23.45] line"). Lines with several timestamps repeat; [offset:±ms]
// shifts every time; word timings "<01:23.45>" are dropped. Sorted by time.
// Text with no timestamps at all comes back as unsynced lines (timeMs -1).
QList<Line> parse(const QString &text);

// Index of the line being sung at positionMs, or -1 before the first one.
// Lines must be synced and sorted, as parse() returns them.
int lineAt(const QList<Line> &lines, qint64 positionMs);

} // namespace Lyrics

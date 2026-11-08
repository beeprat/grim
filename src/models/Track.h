#pragma once

#include <QString>

// Identity is the absolute file path; tags may change between plays.
struct Track
{
    QString path;
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0; // 0 = unknown
    int year = 0;          // 0 = unknown
    QString genre;
    int trackNumber = 0;   // 0 = unknown
    int discNumber = 0;    // 0 = unknown (single-disc albums often leave it out)
    qint64 addedMs = 0;    // when grim first saw the file in the library, ms since epoch; 0 = unknown
    qint64 changedMs = 0;  // when grim last saw it added or its tags/file changed, ms since epoch
};

// Order of tracks within an album: disc, then track number, then file name. Tracks
// without a number go after numbered ones, so a bonus file does not jump to the front.
inline bool albumOrderLess(const Track &a, const Track &b)
{
    const int discA = a.discNumber > 0 ? a.discNumber : 1;
    const int discB = b.discNumber > 0 ? b.discNumber : 1;
    if (discA != discB)
        return discA < discB;
    if ((a.trackNumber > 0) != (b.trackNumber > 0))
        return a.trackNumber > 0;
    if (a.trackNumber != b.trackNumber)
        return a.trackNumber < b.trackNumber;
    return a.path.compare(b.path, Qt::CaseInsensitive) < 0;
}

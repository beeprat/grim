#pragma once

#include "models/Track.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

// Library upkeep lists for the "Check library" window: albums with tags to fill in, and songs saved twice.
namespace LibraryCheck {

struct Gap
{
    QString album;
    QString artist;
    QStringList paths; // every track of the album, in library order (Edit info edits them together)
    bool cover = false; // true: missing
    bool year = false;
    bool genre = false;
    bool trackNumber = false;
};

// Albums (same album and artist, case-insensitive) where the first track has no cover, or any track lacks
// a year, genre or track number. `hasCover` is asked once per album, for its first track.
QList<Gap> missingTags(const QList<Track> &tracks, const std::function<bool(const QString &path)> &hasCover);

// Groups of two or more files with the same file name (case-insensitive) and size: the same song saved
// twice, as phone sync judges it.
QList<QStringList> duplicates(const QList<Track> &tracks, const std::function<qint64(const QString &path)> &sizeOf);

} // namespace LibraryCheck

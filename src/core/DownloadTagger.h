#pragma once

#include "models/Track.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Fits a finished Soulseek download into the library: cleans its title, finds the album it belongs to among
// the tracks already there, and takes that album's folder, artist, album name, year and cover, so a single
// song added to an album does not end up as a second, slightly different album.
namespace DownloadTagger {

// Drops edition notes from a song title: "Help! (Remastered 2009)", "Help! [Deluxe Edition]", "(50th Anniversary)",
// "Help! - Remastered 2009", "Help! - 2015 Remaster" all give "Help!". Other brackets stay ("(Live)").
QString cleanTitle(const QString &title);

// Drops a leading track number from a file name ("01 Help!.m4a", "01 - Help!.m4a", "1-01. Help!.m4a" give
// "Help!.m4a"): the number is in the tags. Only when it is `trackNumber` or zero-padded, so "99 Problems.mp3"
// stays.
QString cleanFileName(const QString &fileName, int trackNumber);

// Album name and artist reduced for comparing: case, "The ", punctuation and edition notes don't count.
QString albumKey(const QString &album);
QString artistKey(const QString &artist);

struct AlbumMatch
{
    bool found = false;
    QString folder;      // where most of the album's tracks are
    QString artist;      // the most common value among the tracks in that folder
    QString album;
    int year = 0;        // 0: none of them has one
    QStringList paths;   // the album's tracks in that folder (for the cover)
};

// The library album the download belongs to: same album name and artist (by the keys above). Nothing is
// found for a download without an album tag.
AlbumMatch findAlbum(const Track &download, const QList<Track> &library);

// Where a download goes when its album is not in the library yet: <library>/<Artist>/<Album>, reusing
// an existing artist folder (same artistKey) and album folder inside it (same albumKey, ignoring a
// "3. " number in front and a " (1971)" year after). Without an album tag: <library>/<Artist>. Empty
// without an artist tag. Names are made safe for any file system.
QString libraryFolderFor(const QString &library, const QString &artist, const QString &album);

// The cover most of these files have (embedded, or a cover file next to them). Empty if none has one.
QByteArray majorityCover(const QStringList &paths);

// Writes the non-empty values (year: non-zero) into the file's tags and, if `cover` is not empty, makes it the
// only picture. Returns false if the file could not be saved.
bool writeTags(const QString &path, const QString &title, const QString &artist, const QString &album, int year,
               const QByteArray &cover);

} // namespace DownloadTagger

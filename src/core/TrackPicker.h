#pragma once

#include "core/SlskdClient.h"
#include "core/SpotifyMatch.h"

#include <QList>
#include <QString>

// Picks Soulseek files for a song from a playlist import: which search results are that song, best
// first. No network; SpotifyController searches and downloads.
namespace TrackPicker {

// Soulseek search text: the first artist and the title without its extras ("- 2004 Remaster").
QString searchText(const Spotify::RemoteTrack &track);

// Searches to try in turn while one finds nothing. The Soulseek server makes clients ignore
// searches containing some artist names ("deftones" finds nothing, "metallica" thousands), so after
// the plain one: the artist's first word as a wildcard ("*eftones my own summer"), then the title
// with the album and no artist at all.
QStringList searchTexts(const Spotify::RemoteTrack &track);
// The same for a whole album: artist + album, wildcard artist + album, album alone.
QStringList albumSearchTexts(const QString &artist, const QString &album);

// Indices into `files` of the results that are this song, best first:
// - an audio file whose name has every word of the title and whose path names one of the artists;
// - within 5 s of the song's length (files without a length pass, ranked lower);
// - no live / karaoke / instrumental / remix / cover / acoustic / demo version unless the title has
//   that word too; lossy files under 192 kbps are left out;
// - `format` ("flac", "mp3", …; empty = any) first, other formats after it; then a free upload slot,
//   a short queue and a fast uploader.
QList<int> rank(const QList<SlskdSearchFile> &files, const Spotify::RemoteTrack &track, const QString &format);

} // namespace TrackPicker

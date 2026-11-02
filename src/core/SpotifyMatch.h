#pragma once

#include "models/Track.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// Pieces of the Spotify playlist import that need no network: reading the playlist page and
// finding a Spotify track among the library's files.
namespace Spotify {

struct RemoteTrack;

// "https://open.spotify.com/playlist/<id>?si=…", "spotify:album:<id>", … → {"playlist" or
// "album", id}; empty kind if it is not such a link.
struct Link
{
    QString kind;
    QString id;
};
Link parseLink(const QString &text);

// The embed page (open.spotify.com/embed/<kind>/<id>) carries the list as JSON in its
// __NEXT_DATA__ script: name and up to 100 tracks. False if the page has no list.
bool parseEmbedPage(const QByteArray &html, QString *name, QList<RemoteTrack> *tracks);

// For comparing names: lower case, accents dropped, "&" read as "and", only letters, digits and
// single spaces left.
QString normalize(const QString &text);
// A title without its extras: "(feat. X)", "[Remastered]", " - 2009 Remaster", " - Live".
QString baseTitle(const QString &title);
// The names in a local artist tag: "A & B", "A feat. B", "A, B" give A and B (normalized, without
// a leading "the").
QStringList artistNames(const QString &artistTag);

struct RemoteTrack
{
    QString title;
    QStringList artists;
    QString album;
    qint64 durationMs = 0;
};

// Index of the library's tracks by title and artist, built once per import.
class Matcher
{
public:
    explicit Matcher(const QList<Track> &tracks);

    // Path of the library file for this track, empty if none. Several files with the same title
    // and artist: the one closest in length, then the one on the same album.
    QString match(const RemoteTrack &remote) const;

private:
    QList<Track> m_tracks;
    QHash<QString, QList<int>> m_byKey; // baseTitle + artist name → indices into m_tracks
};

} // namespace Spotify

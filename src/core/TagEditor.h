#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

// Reads and writes the tags people edit by hand (right-click → "Edit info…"): title, artist,
// album artist, album, genre, year, track number, disc number, composer, label and the front cover.
namespace TagEditor {

// Keys for read() and write().
inline constexpr auto kTitle = "title";
inline constexpr auto kArtist = "artist";
inline constexpr auto kAlbumArtist = "albumArtist";
inline constexpr auto kAlbum = "album";
inline constexpr auto kGenre = "genre";
inline constexpr auto kYear = "year";
inline constexpr auto kTrack = "track";
inline constexpr auto kDisc = "disc";
inline constexpr auto kComposer = "composer";
inline constexpr auto kLabel = "label";

enum class Cover { Keep, Replace, Remove };

// The fields above as strings (empty when unset) plus "hasCover". Empty map if unreadable.
QVariantMap read(const QString &path);

// Sets every field present in fields (an empty string clears it); absent fields stay. A cover
// for Replace is JPEG or PNG bytes. Empty on success, else a message for the user.
QString write(const QString &path, const QVariantMap &fields, Cover cover = Cover::Keep,
              const QByteArray &coverData = {});

} // namespace TagEditor

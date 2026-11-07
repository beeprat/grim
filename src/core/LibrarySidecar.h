#pragma once

#include <QString>

class LibraryDatabase;

// `.grim-library.json` in the music folder: ratings, favorites, volume adjustments and playlists, keyed by
// path relative to that folder. It travels with a backup of the music, so another grim. (another computer,
// Linux) picks them up from it. The audio files are never touched.
namespace LibrarySidecar {

inline constexpr auto kFileName = ".grim-library.json";

// Writes what `data` holds for songs under `libraryFolder`. False if it could not be written.
bool write(const LibraryDatabase &data, const QString &libraryFolder);
// Adds what the file has and `data` lacks: unrated songs get the file's rating, favorites are joined,
// missing playlists are created. Nothing in `data` is overwritten. True if anything changed.
bool read(LibraryDatabase &data, const QString &libraryFolder);

} // namespace LibrarySidecar

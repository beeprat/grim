#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace LibraryFiles {

// File extensions the player can decode, lower case without dot.
const QStringList &playableExtensions();
bool isPlayable(const QString &filePath);

// Moves a finished download into the library, keeping its folder relative to
// downloadsRoot (e.g. "<library>/1967 - Strange Days/04 - Unhappy Girl.m4a").
// A file outside downloadsRoot goes into a folder named after its parent.
// Never overwrites: a name clash gets " (2)", " (3)", ... Removes the source
// folder if it is left empty. Returns the new path, or empty with *error set.
QString moveIntoLibrary(const QString &filePath, const QString &downloadsRoot, const QString &libraryRoot,
                        QString *error);

// Deletes these music files, then each folder they were in that no longer holds any
// playable file (cover art, .lrc, playlists and empty subfolders go with it), never the
// library root or anything outside it. `remove` deletes one file or folder; the app passes
// the Recycle Bin (QFile::moveToTrash). Returns the paths that could not be removed.
QStringList deleteAlbum(const QStringList &filePaths, const QString &libraryRoot,
                        const std::function<bool(const QString &)> &remove);

} // namespace LibraryFiles

#pragma once

#include "models/Track.h"

#include <QByteArray>
#include <QString>

namespace TagReader {

// Reads tags and duration with TagLib. Never fails: an unreadable or untagged
// file still gives a Track with its path and the file name as title.
Track read(const QString &filePath);

// Encoded image bytes (JPEG/PNG) of the embedded front cover, or else a
// cover.jpg / folder.jpg / front.jpg (or .png) next to the file. Empty if none.
// Safe to call from any thread.
QByteArray readCover(const QString &filePath);

} // namespace TagReader

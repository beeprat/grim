#pragma once

#include "models/Track.h"

#include <QHash>
#include <QList>
#include <QString>

// The library as last scanned, kept on disk so the next start shows it at once, and the
// incremental scan that brings it up to date: only new or changed files (size or modification
// time differ) are read with TagLib again; the rest come from the index.
namespace LibraryIndex {

struct Entry
{
    Track track;
    qint64 size = 0;
    qint64 modifiedMs = 0; // last modification, ms since epoch
};

struct ScanResult
{
    QList<Entry> entries; // every playable file under the folder
    bool changed = false; // differs from `known` (files added, removed or modified)
};

// Entries keyed by lower-case path, as load() returns and scan() takes them.
using Known = QHash<QString, Entry>;

// Empty if the file is missing, unreadable, from another format version, or for another folder.
Known load(const QString &indexFile, const QString &folder);
bool save(const QString &indexFile, const QString &folder, const QList<Entry> &entries);

// Walks `folder` (skipping half-written ".converting" files). `read` reads one file's tags;
// it is only called for files not in `known` with the same size and modification time.
ScanResult scan(const QString &folder, const Known &known, Track (*read)(const QString &path));

QList<Track> tracksOf(const QList<Entry> &entries);
Known keyed(const QList<Entry> &entries);

} // namespace LibraryIndex

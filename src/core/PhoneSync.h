#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

// Mirrors the library's songs (and .lrc lyrics) into a phone app's folder, keeping the library's
// folder layout. Only those file types are ever copied or deleted: anything else in the phone's
// folder (the app's own files, covers) is left alone.
namespace PhoneSync {

struct Plan
{
    QStringList added;      // relative paths not on the phone yet
    QStringList updated;    // on the phone, but a different size or changed since (e.g. tags edited)
    QStringList copy;       // added + updated: what apply() copies
    QStringList remove;     // on the phone, no longer in the library
    QStringList duplicates; // skipped: the same song (file name and size) is already elsewhere
    qint64 copyBytes = 0;
    // Library path → the same file's path on the phone where it is spelled differently (iOS keeps
    // names decomposed: "o" + "¨" instead of "ö"). apply() writes updates over that file.
    QHash<QString, QString> phoneNames;
};

// Songs and lyrics, by file extension.
bool isSynced(const QString &relativePath);

// Compares the two folders, names compared in Unicode NFC. A file is updated when its size
// differs or the library's copy was modified after the phone's. Slow on a phone (every file is
// asked for its size and time). With
// skipDuplicates, a new song whose file name and size match a file already on the phone (in
// another folder) or another library file is not copied, and that phone copy is kept.
Plan plan(const QString &library, const QString &phone, bool skipDuplicates = false);

// Applies a plan: deletions first (then empty folders), then copies. progress(done, total) after
// each file. Stops early when cancel is set. Returns an error message, or empty on success.
QString apply(const Plan &plan, const QString &library, const QString &phone,
              const std::function<void(int done, int total)> &progress, const std::atomic_bool &cancel);

} // namespace PhoneSync

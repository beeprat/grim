#pragma once

#include <QString>

class QSettings;

// Moves user data from the old "MusicPlayer" names to "grimPlayer" on the first start after the
// rename. Never overwrites: if the new location already has data, the old one is left alone.
namespace UserDataMigration {

// Renames folder `from` to `to` (creating `to`'s parent) when `from` exists and `to` does not,
// then removes `from`'s parent if that left it empty. True if it moved.
bool moveFolder(const QString &from, const QString &to);

// Copies every key of `from` into `to` when `to` is empty and `from` is not, then clears `from`
// once `to` is written. True if it copied.
bool moveSettings(QSettings &from, QSettings &to);

} // namespace UserDataMigration

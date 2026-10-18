#include "core/UserDataMigration.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace UserDataMigration {

bool moveFolder(const QString &from, const QString &to)
{
    if (!QFileInfo(from).isDir() || QFileInfo::exists(to))
        return false;
    const QString target = QDir::cleanPath(to);
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QDir().rename(from, target))
        return false; // e.g. a file in use; the old folder stays and is still readable by an older build
    QDir parent = QFileInfo(QDir::cleanPath(from)).dir();
    if (parent.isEmpty())
        parent.removeRecursively(); // only the now-empty old organization folder
    return true;
}

bool moveSettings(QSettings &from, QSettings &to)
{
    const QStringList keys = from.allKeys();
    if (keys.isEmpty() || !to.allKeys().isEmpty())
        return false;
    for (const QString &key : keys)
        to.setValue(key, from.value(key));
    to.sync();
    if (to.status() != QSettings::NoError)
        return false; // keep the old settings when the new ones could not be written
    from.clear();
    from.sync();
    return true;
}

} // namespace UserDataMigration

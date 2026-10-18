#include "core/LibraryFiles.h"

#include <QDirIterator>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace LibraryFiles {

const QStringList &playableExtensions()
{
    static const QStringList extensions{u"mp3"_s, u"flac"_s, u"wav"_s, u"m4a"_s, u"mp4"_s, u"aac"_s};
    return extensions;
}

bool isPlayable(const QString &filePath)
{
    return playableExtensions().contains(QFileInfo(filePath).suffix().toLower());
}

QString moveIntoLibrary(const QString &filePath, const QString &downloadsRoot, const QString &libraryRoot,
                        QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QString();
    };

    const QFileInfo source(filePath);
    if (!source.isFile())
        return fail(QCoreApplication::translate("LibraryFiles", "%1 does not exist").arg(filePath));

    QString relativeDir = QDir(downloadsRoot).relativeFilePath(source.absolutePath());
    if (downloadsRoot.isEmpty() || relativeDir.startsWith(u".."_s) || QDir::isAbsolutePath(relativeDir))
        relativeDir = source.dir().dirName();
    if (relativeDir == u"."_s)
        relativeDir.clear();

    const QDir targetDir(QDir(libraryRoot).filePath(relativeDir));
    if (!QDir().mkpath(targetDir.path()))
        return fail(QCoreApplication::translate("LibraryFiles", "Cannot create %1").arg(targetDir.path()));

    QString target = targetDir.filePath(source.fileName());
    if (QFileInfo(target).absoluteFilePath() == source.absoluteFilePath())
        return source.absoluteFilePath(); // already in place (library folder is the downloads folder)
    for (int n = 2; QFileInfo::exists(target); ++n)
        target = targetDir.filePath(u"%1 (%2).%3"_s.arg(source.completeBaseName()).arg(n).arg(source.suffix()));

    // rename() fails across drives; fall back to copy + delete.
    if (!QFile::rename(source.absoluteFilePath(), target)) {
        if (!QFile::copy(source.absoluteFilePath(), target))
            return fail(QCoreApplication::translate("LibraryFiles", "Cannot move %1 to %2").arg(source.fileName(), target));
        QFile::remove(source.absoluteFilePath());
    }

    QDir().rmdir(source.absolutePath()); // only succeeds if now empty
    return QFileInfo(target).absoluteFilePath();
}

QStringList deleteAlbum(const QStringList &filePaths, const QString &libraryRoot,
                        const std::function<bool(const QString &)> &remove)
{
    QStringList failed;
    QStringList folders;
    for (const QString &path : filePaths) {
        if (QFileInfo::exists(path) && !remove(path)) {
            failed.append(path);
            continue;
        }
        const QString folder = QFileInfo(path).absolutePath();
        if (!folders.contains(folder))
            folders.append(folder);
    }

    const QString root = QDir::cleanPath(QFileInfo(libraryRoot).absoluteFilePath());
    for (const QString &folder : folders) {
        const QString clean = QDir::cleanPath(folder);
        // Strictly inside the library: never the root itself, never elsewhere on the disk.
        if (!clean.startsWith(root + u'/', Qt::CaseInsensitive) || !QFileInfo::exists(clean))
            continue;
        QDirIterator it(clean, QDir::Files, QDirIterator::Subdirectories);
        bool hasMusic = false;
        while (it.hasNext() && !hasMusic)
            hasMusic = isPlayable(it.next());
        if (!hasMusic && !remove(clean))
            failed.append(clean);
    }
    return failed;
}

} // namespace LibraryFiles

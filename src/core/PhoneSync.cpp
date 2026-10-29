#include "core/PhoneSync.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

using namespace Qt::StringLiterals;

namespace {

// A copy counts as current when it is at most this much older than the library's file
// (file systems round times differently).
constexpr qint64 kTimeSlackMs = 2000;

struct Entry
{
    QString path; // relative, as spelled on that disk
    qint64 size = 0;
    qint64 modifiedMs = 0;
};

// NFC relative path → every synced file under root.
QHash<QString, Entry> scan(const QString &root)
{
    QHash<QString, Entry> files;
    const QDir base(root);
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString relative = base.relativeFilePath(info.filePath());
        if (PhoneSync::isSynced(relative))
            files.insert(relative.normalized(QString::NormalizationForm_C),
                         {relative, info.size(), info.lastModified().toMSecsSinceEpoch()});
    }
    return files;
}

} // namespace

bool PhoneSync::isSynced(const QString &relativePath)
{
    static const QSet<QString> kTypes = {u"mp3"_s, u"m4a"_s, u"aac"_s, u"flac"_s, u"wav"_s, u"ogg"_s,
                                         u"opus"_s, u"alac"_s, u"aiff"_s, u"lrc"_s};
    const QFileInfo info(relativePath);
    // Skip hidden files and half-converted downloads (".converting.").
    return !info.fileName().startsWith(u'.') && !relativePath.contains(".converting."_L1)
           && kTypes.contains(info.suffix().toLower());
}

PhoneSync::Plan PhoneSync::plan(const QString &library, const QString &phone, bool skipDuplicates)
{
    const QHash<QString, Entry> local = scan(library);
    const QHash<QString, Entry> remote = scan(phone);
    // A song's identity for duplicates: lower-case file name plus size.
    const auto identity = [](const QString &key, qint64 size) {
        return QFileInfo(key).fileName().toLower() + u'\n' + QString::number(size);
    };
    QHash<QString, QString> remoteByIdentity; // identity → key on the phone
    for (auto it = remote.cbegin(); it != remote.cend(); ++it)
        remoteByIdentity.insert(identity(it.key(), it->size), it.key());

    Plan result;
    QSet<QString> kept;   // phone keys that stand in for a skipped duplicate
    QSet<QString> copied; // identities already planned for copying
    QStringList keys = local.keys();
    keys.sort();
    for (const QString &key : std::as_const(keys)) {
        const Entry &file = local[key];
        const auto there = remote.constFind(key);
        if (there != remote.cend()) {
            if (there->path != file.path)
                result.phoneNames.insert(file.path, there->path);
            if (there->size != file.size || file.modifiedMs > there->modifiedMs + kTimeSlackMs) {
                result.updated.append(file.path);
                result.copyBytes += file.size;
            }
            continue;
        }
        const QString id = identity(key, file.size);
        if (skipDuplicates) {
            const auto elsewhere = remoteByIdentity.constFind(id);
            if (elsewhere != remoteByIdentity.cend() && !local.contains(elsewhere.value())) {
                kept.insert(elsewhere.value());
                result.duplicates.append(file.path);
                continue;
            }
            if (copied.contains(id)) {
                result.duplicates.append(file.path);
                continue;
            }
        }
        copied.insert(id);
        result.added.append(file.path);
        result.copyBytes += file.size;
    }
    for (auto it = remote.cbegin(); it != remote.cend(); ++it)
        if (!local.contains(it.key()) && !kept.contains(it.key()))
            result.remove.append(it->path);
    result.remove.sort();
    result.copy = result.added + result.updated;
    return result;
}

QString PhoneSync::apply(const Plan &plan, const QString &library, const QString &phone,
                         const std::function<void(int, int)> &progress, const std::atomic_bool &cancel)
{
    const QDir from(library);
    const QDir to(phone);
    const int total = int(plan.remove.size() + plan.copy.size());
    int done = 0;

    for (const QString &relative : plan.remove) {
        if (cancel)
            return {};
        if (!QFile::remove(to.filePath(relative)))
            return QCoreApplication::translate("PhoneSync", "Could not delete %1 on the phone.").arg(relative);
        // Folders left empty go too, up to (not including) the app's folder.
        QDir folder = QFileInfo(to.filePath(relative)).dir();
        while (folder.absolutePath() != to.absolutePath() && folder.isEmpty()) {
            const QString name = folder.dirName();
            if (!folder.cdUp() || !folder.rmdir(name))
                break;
        }
        progress(++done, total);
    }

    for (const QString &relative : plan.copy) {
        if (cancel)
            return {};
        const QString target = to.filePath(plan.phoneNames.value(relative, relative));
        QDir().mkpath(QFileInfo(target).path());
        QFile::remove(target); // a changed file is replaced
        if (!QFile::copy(from.filePath(relative), target)) {
            QFile::remove(target); // no half-copied song
            return QCoreApplication::translate("PhoneSync", "Could not copy %1 to the phone (is it full?).").arg(relative);
        }
        progress(++done, total);
    }
    return {};
}

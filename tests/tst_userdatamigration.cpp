#include "core/UserDataMigration.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

bool write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

} // namespace

class TestUserDataMigration : public QObject
{
    Q_OBJECT

private slots:
    void movesFolderAndRemovesEmptyOldParent()
    {
        QTemporaryDir root;
        const QString from = root.filePath(u"MusicPlayer/MusicPlayer"_s);
        const QString to = root.filePath(u"grimPlayer/grimPlayer"_s);
        QVERIFY(write(from + u"/stats.sqlite"_s, "stats"));
        QVERIFY(write(from + u"/translations/Bosanski.txt"_s, "x"));

        QVERIFY(UserDataMigration::moveFolder(from, to));
        QVERIFY(QFile::exists(to + u"/stats.sqlite"_s));
        QVERIFY(QFile::exists(to + u"/translations/Bosanski.txt"_s));
        QVERIFY(!QFileInfo::exists(root.filePath(u"MusicPlayer"_s)));

        // Already moved, or nothing to move: no-op.
        QVERIFY(!UserDataMigration::moveFolder(from, to));
    }

    void neverOverwritesExistingNewData()
    {
        QTemporaryDir root;
        const QString from = root.filePath(u"MusicPlayer/MusicPlayer"_s);
        const QString to = root.filePath(u"grimPlayer/grimPlayer"_s);
        QVERIFY(write(from + u"/stats.sqlite"_s, "old"));
        QVERIFY(write(to + u"/stats.sqlite"_s, "new"));
        QVERIFY(!UserDataMigration::moveFolder(from, to));
        QFile kept(to + u"/stats.sqlite"_s);
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), QByteArray("new"));
        QVERIFY(QFile::exists(from + u"/stats.sqlite"_s)); // old data untouched
    }

    void movesSettingsOnlyIntoEmptyStore()
    {
        QTemporaryDir root;
        QSettings from(root.filePath(u"old.ini"_s), QSettings::IniFormat);
        QSettings to(root.filePath(u"new.ini"_s), QSettings::IniFormat);
        from.setValue(u"theme/accentIndex"_s, 5);
        from.setValue(u"lastfm/apiKey"_s, u"abc"_s);

        QVERIFY(UserDataMigration::moveSettings(from, to));
        QCOMPARE(to.value(u"theme/accentIndex"_s).toInt(), 5);
        QCOMPARE(to.value(u"lastfm/apiKey"_s).toString(), u"abc"_s);
        QVERIFY(from.allKeys().isEmpty());

        // The new store has settings now: a second run changes nothing.
        from.setValue(u"theme/accentIndex"_s, 1);
        QVERIFY(!UserDataMigration::moveSettings(from, to));
        QCOMPARE(to.value(u"theme/accentIndex"_s).toInt(), 5);
    }
};

QTEST_GUILESS_MAIN(TestUserDataMigration)
#include "tst_userdatamigration.moc"

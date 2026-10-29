#include "core/PhoneSync.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

} // namespace

class TestPhoneSync : public QObject
{
    Q_OBJECT

private slots:
    void mirrorsSongsAndLeavesOtherFilesAlone()
    {
        QTemporaryDir library;
        QTemporaryDir phone;
        write(library.filePath(u"A/1. X (1970)/new.m4a"_s), "new");
        write(library.filePath(u"A/1. X (1970)/same.m4a"_s), "same");
        write(library.filePath(u"A/1. X (1970)/changed.m4a"_s), "changed!");
        write(library.filePath(u"A/1. X (1970)/Folder.jpg"_s), "cover"); // not synced
        write(phone.filePath(u"A/1. X (1970)/same.m4a"_s), "same");
        write(phone.filePath(u"A/1. X (1970)/changed.m4a"_s), "old");
        write(phone.filePath(u"B/1. Gone (1980)/gone.mp3"_s), "gone");
        write(phone.filePath(u"add-files-here.txt"_s), "the app's own"); // never touched

        const PhoneSync::Plan plan = PhoneSync::plan(library.path(), phone.path());
        QCOMPARE(plan.added, QStringList{u"A/1. X (1970)/new.m4a"_s});
        QCOMPARE(plan.updated, QStringList{u"A/1. X (1970)/changed.m4a"_s});
        QCOMPARE(plan.remove, QStringList{u"B/1. Gone (1980)/gone.mp3"_s});
        QCOMPARE(plan.copyBytes, qint64(11));

        std::atomic_bool cancel{false};
        int calls = 0;
        QVERIFY(PhoneSync::apply(plan, library.path(), phone.path(), [&](int, int) { ++calls; }, cancel).isEmpty());
        QCOMPARE(calls, 3);
        QVERIFY(!QDir(phone.filePath(u"B"_s)).exists()); // emptied folders removed
        QVERIFY(QFile::exists(phone.filePath(u"add-files-here.txt"_s)));
        QVERIFY(!QFile::exists(phone.filePath(u"A/1. X (1970)/Folder.jpg"_s)));
        QFile changed(phone.filePath(u"A/1. X (1970)/changed.m4a"_s));
        QVERIFY(changed.open(QIODevice::ReadOnly));
        QCOMPARE(changed.readAll(), QByteArray("changed!"));

        const PhoneSync::Plan after = PhoneSync::plan(library.path(), phone.path());
        QVERIFY(after.copy.isEmpty() && after.remove.isEmpty());
    }

    void matchesNamesAcrossUnicodeForms()
    {
        QTemporaryDir library;
        QTemporaryDir phone;
        const QString composed = u"Mot\u00f6rhead/Ace of Spades.m4a"_s;    // "ö", as on Linux
        const QString decomposed = u"Moto\u0308rhead/Ace of Spades.m4a"_s; // "o" + "¨", as on iOS
        write(library.filePath(composed), "song");
        write(phone.filePath(decomposed), "song");
        const PhoneSync::Plan plan = PhoneSync::plan(library.path(), phone.path());
        QVERIFY(plan.added.isEmpty());
        QVERIFY(plan.remove.isEmpty());
    }

    void updatesFilesChangedSince()
    {
        QTemporaryDir library;
        QTemporaryDir phone;
        const QString song = u"A/song.m4a"_s;
        write(library.filePath(song), "tags");
        write(phone.filePath(song), "tags"); // same size
        QFile phoneFile(phone.filePath(song));
        QVERIFY(phoneFile.open(QIODevice::ReadWrite));
        QVERIFY(phoneFile.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
        phoneFile.close();
        QCOMPARE(PhoneSync::plan(library.path(), phone.path()).updated, QStringList{song});
    }

    void skipsDuplicates()
    {
        QTemporaryDir library;
        QTemporaryDir phone;
        write(library.filePath(u"A/1. X (1970)/song.m4a"_s), "song");
        write(library.filePath(u"Compilations/Best/song.m4a"_s), "song"); // same song twice locally
        write(phone.filePath(u"Old place/song.m4a"_s), "song");          // already on the phone elsewhere

        const PhoneSync::Plan off = PhoneSync::plan(library.path(), phone.path());
        QCOMPARE(off.added.size(), 2);
        QCOMPARE(off.remove, QStringList{u"Old place/song.m4a"_s});

        const PhoneSync::Plan on = PhoneSync::plan(library.path(), phone.path(), true);
        QVERIFY(on.added.isEmpty());
        QCOMPARE(on.duplicates.size(), 2);
        QVERIFY(on.remove.isEmpty()); // the phone's copy stays
    }
};

QTEST_GUILESS_MAIN(TestPhoneSync)
#include "tst_phonesync.moc"

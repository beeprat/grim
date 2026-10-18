#include "core/LibraryFiles.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

bool touch(const QString &path, const QByteArray &content = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

} // namespace

class TestLibraryFiles : public QObject
{
    Q_OBJECT

private slots:
    void keepsAlbumFolderAndRemovesEmptySource()
    {
        QTemporaryDir root;
        const QString downloads = root.filePath(u"downloads"_s);
        const QString library = root.filePath(u"Music"_s);
        const QString file = downloads + u"/1967 - Strange Days/04 - Unhappy Girl.m4a"_s;
        QVERIFY(touch(file));

        QString error;
        const QString moved = LibraryFiles::moveIntoLibrary(file, downloads, library, &error);
        QVERIFY2(!moved.isEmpty(), qPrintable(error));
        QCOMPARE(moved, QFileInfo(library + u"/1967 - Strange Days/04 - Unhappy Girl.m4a"_s).absoluteFilePath());
        QVERIFY(QFile::exists(moved));
        QVERIFY(!QFile::exists(file));
        QVERIFY(!QDir(downloads + u"/1967 - Strange Days"_s).exists());
    }

    void neverOverwrites()
    {
        QTemporaryDir root;
        const QString downloads = root.filePath(u"downloads"_s);
        const QString library = root.filePath(u"Music"_s);
        QVERIFY(touch(library + u"/Album/song.m4a"_s, "existing"));
        QVERIFY(touch(downloads + u"/Album/song.m4a"_s, "new"));
        QVERIFY(touch(downloads + u"/Album/other.m4a"_s));

        const QString moved = LibraryFiles::moveIntoLibrary(downloads + u"/Album/song.m4a"_s, downloads, library, nullptr);
        QCOMPARE(QFileInfo(moved).fileName(), u"song (2).m4a"_s);
        QFile existing(library + u"/Album/song.m4a"_s);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), "existing");
        QVERIFY(QDir(downloads + u"/Album"_s).exists()); // still holds other.m4a
    }

    void fileOutsideDownloadsUsesParentFolderName()
    {
        QTemporaryDir root;
        const QString elsewhere = root.filePath(u"elsewhere/Some Album/track.m4a"_s);
        QVERIFY(touch(elsewhere));
        const QString moved = LibraryFiles::moveIntoLibrary(elsewhere, root.filePath(u"downloads"_s),
                                                            root.filePath(u"Music"_s), nullptr);
        QCOMPARE(moved, QFileInfo(root.filePath(u"Music/Some Album/track.m4a"_s)).absoluteFilePath());
    }

    void sameFolderIsANoOp()
    {
        QTemporaryDir root;
        const QString file = root.filePath(u"Album/track.m4a"_s);
        QVERIFY(touch(file));
        QCOMPARE(LibraryFiles::moveIntoLibrary(file, root.path(), root.path(), nullptr), QFileInfo(file).absoluteFilePath());
        QVERIFY(QFile::exists(file));
    }

    void missingFileReportsError()
    {
        QString error;
        QVERIFY(LibraryFiles::moveIntoLibrary(u"C:/nope/x.m4a"_s, u"C:/nope"_s, u"C:/lib"_s, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void deleteAlbumTakesItsFolderOnlyWhenNoMusicIsLeft()
    {
        QTemporaryDir root;
        const QString library = root.filePath(u"Music"_s);
        const QString album = library + u"/Camel/Mirage"_s;
        const QString shared = library + u"/Singles"_s;
        QVERIFY(touch(album + u"/01 Freefall.mp3"_s));
        QVERIFY(touch(album + u"/02 Supertwister.mp3"_s));
        QVERIFY(touch(album + u"/cover.jpg"_s));
        QVERIFY(touch(album + u"/scans/back.jpg"_s));
        QVERIFY(touch(shared + u"/a.mp3"_s));
        QVERIFY(touch(shared + u"/b.mp3"_s));

        QStringList removed;
        const auto remove = [&](const QString &path) {
            removed.append(path);
            const QFileInfo info(path);
            return info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
        };

        // The whole album: its files, then the folder with the cover and scans.
        QVERIFY(LibraryFiles::deleteAlbum({album + u"/01 Freefall.mp3"_s, album + u"/02 Supertwister.mp3"_s},
                                          library, remove).isEmpty());
        QVERIFY(!QFileInfo::exists(album));
        QVERIFY(QFileInfo::exists(library + u"/Camel"_s)); // only the album's own folder

        // One file of a folder that keeps other music: the folder stays.
        QVERIFY(LibraryFiles::deleteAlbum({shared + u"/a.mp3"_s}, library, remove).isEmpty());
        QVERIFY(QFileInfo::exists(shared + u"/b.mp3"_s));

        // Files directly in the library root never take the root with them.
        QVERIFY(touch(library + u"/loose.mp3"_s));
        removed.clear();
        QVERIFY(LibraryFiles::deleteAlbum({library + u"/loose.mp3"_s}, library, remove).isEmpty());
        QCOMPARE(removed, QStringList{library + u"/loose.mp3"_s});
        QVERIFY(QFileInfo::exists(library));
    }

    void playable()
    {
        QVERIFY(LibraryFiles::isPlayable(u"a/B.M4A"_s));
        QVERIFY(LibraryFiles::isPlayable(u"b.flac"_s));
        QVERIFY(!LibraryFiles::isPlayable(u"c.ogg"_s));
        QVERIFY(!LibraryFiles::isPlayable(u"cover.jpg"_s));
    }
};

QTEST_GUILESS_MAIN(TestLibraryFiles)
#include "tst_libraryfiles.moc"

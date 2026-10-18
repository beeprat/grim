#include "core/LibraryIndex.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

int s_reads = 0;

// Stands in for TagReader: counts calls and names the track after its file.
Track fakeRead(const QString &path)
{
    ++s_reads;
    Track track;
    track.path = path;
    track.title = QFileInfo(path).completeBaseName();
    track.artist = u"Camel"_s;
    track.year = 1974;
    return track;
}

bool write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

} // namespace

class TestLibraryIndex : public QObject
{
    Q_OBJECT

private slots:
    void rescanReadsOnlyNewOrChangedFiles()
    {
        QTemporaryDir dir;
        const QString folder = dir.filePath(u"Music"_s);
        QVERIFY(write(folder + u"/Mirage/Freefall.mp3"_s, "aaaa"));
        QVERIFY(write(folder + u"/Mirage/Supertwister.flac"_s, "bbbb"));
        QVERIFY(write(folder + u"/Mirage/cover.jpg"_s, "not music"));
        QVERIFY(write(folder + u"/Mirage/Half.converting.m4a"_s, "partial"));

        // First scan: every music file is read.
        s_reads = 0;
        const LibraryIndex::ScanResult first = LibraryIndex::scan(folder, {}, fakeRead);
        QCOMPARE(first.entries.size(), 2);
        QCOMPARE(s_reads, 2);
        QVERIFY(first.changed);

        // Saved and loaded back unchanged.
        const QString indexFile = dir.filePath(u"index.bin"_s);
        QVERIFY(LibraryIndex::save(indexFile, folder, first.entries));
        LibraryIndex::Known known = LibraryIndex::load(indexFile, folder);
        QCOMPARE(known.size(), 2);
        const LibraryIndex::Entry loaded = known.value((folder + u"/Mirage/Freefall.mp3"_s).toLower());
        QCOMPARE(loaded.track.title, u"Freefall"_s);
        QCOMPARE(loaded.track.year, 1974);
        QVERIFY(LibraryIndex::load(indexFile, dir.filePath(u"Other"_s)).isEmpty()); // other folder

        // Nothing changed on disk: no file is read, tracks come from the index.
        known[(folder + u"/Mirage/Freefall.mp3"_s).toLower()].track.title = u"From the index"_s;
        s_reads = 0;
        const LibraryIndex::ScanResult same = LibraryIndex::scan(folder, known, fakeRead);
        QCOMPARE(s_reads, 0);
        QVERIFY(!same.changed);
        bool fromIndex = false;
        for (const LibraryIndex::Entry &entry : same.entries)
            fromIndex = fromIndex || entry.track.title == u"From the index"_s;
        QVERIFY(fromIndex);

        // A retagged file (new size) and a new file are read; the rest is reused.
        QVERIFY(write(folder + u"/Mirage/Freefall.mp3"_s, "retagged, longer"));
        QVERIFY(write(folder + u"/Mirage/Earthrise.mp3"_s, "cccc"));
        s_reads = 0;
        const LibraryIndex::ScanResult updated = LibraryIndex::scan(folder, known, fakeRead);
        QCOMPARE(updated.entries.size(), 3);
        QCOMPARE(s_reads, 2);
        QVERIFY(updated.changed);

        // A deleted file counts as a change though nothing needs reading.
        QVERIFY(QFile::remove(folder + u"/Mirage/Earthrise.mp3"_s));
        s_reads = 0;
        const LibraryIndex::ScanResult removed = LibraryIndex::scan(folder, LibraryIndex::keyed(updated.entries), fakeRead);
        QCOMPARE(removed.entries.size(), 2);
        QCOMPARE(s_reads, 0);
        QVERIFY(removed.changed);
    }

    void damagedIndexIsIgnored()
    {
        QTemporaryDir dir;
        const QString indexFile = dir.filePath(u"index.bin"_s);
        QVERIFY(write(indexFile, "garbage that is not an index"));
        QVERIFY(LibraryIndex::load(indexFile, dir.path()).isEmpty());
        QVERIFY(LibraryIndex::load(dir.filePath(u"missing.bin"_s), dir.path()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLibraryIndex)
#include "tst_libraryindex.moc"

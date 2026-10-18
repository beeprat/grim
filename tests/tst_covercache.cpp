#include "core/CoverCache.h"

#include <fileref.h>

#include <QFileInfo>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace Qt::StringLiterals;

namespace {

bool writeSilentWav(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + 4);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(2) << quint32(44100) << quint32(44100 * 4) << quint16(4)
        << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(4) << quint32(0);
    return out.status() == QDataStream::Ok;
}

bool embedCover(const QString &audioPath, const QColor &color)
{
    QImage image(400, 300, QImage::Format_RGB32);
    image.fill(color);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");

    const std::filesystem::path path = QFileInfo(audioPath).filesystemFilePath();
    TagLib::FileRef file(path.c_str());
    TagLib::VariantMap picture;
    picture.insert("data", TagLib::ByteVector(png.constData(), static_cast<unsigned int>(png.size())));
    picture.insert("mimeType", TagLib::String("image/png"));
    picture.insert("pictureType", TagLib::String("Front Cover"));
    return file.setComplexProperties("PICTURE", {picture}) && file.save();
}

int fileCount(const QString &dir)
{
    return static_cast<int>(QDir(dir).entryList(QDir::Files).size());
}

} // namespace

class TestCoverCache : public QObject
{
    Q_OBJECT

private slots:
    void scalesAndCachesOnDisk()
    {
        QTemporaryDir dir;
        const QString song = dir.filePath(u"song.wav"_s);
        const QString cacheDir = dir.filePath(u"covers"_s);
        QVERIFY(writeSilentWav(song));
        QVERIFY(embedCover(song, Qt::red));

        {
            CoverCache cache(cacheDir);
            const QImage image = cache.load(song, QSize(100, 100));
            QCOMPARE(image.size(), QSize(133, 100)); // aspect kept, covers the requested box
            QCOMPARE(fileCount(cacheDir), 1);
            QCOMPARE(cache.load(song, QSize(100, 100)).size(), image.size()); // memory hit
            QCOMPARE(fileCount(cacheDir), 1);
        }

        // A new cache (next app start) reads the thumbnail from disk.
        CoverCache cache(cacheDir);
        const QImage fromDisk = cache.load(song, QSize(100, 100));
        QCOMPARE(fromDisk.size(), QSize(133, 100));
        QVERIFY(qRed(fromDisk.pixel(50, 50)) > 200);
        QCOMPARE(fileCount(cacheDir), 1);
    }

    void retaggedFileGetsNewThumbnail()
    {
        QTemporaryDir dir;
        const QString song = dir.filePath(u"song.wav"_s);
        QVERIFY(writeSilentWav(song));
        QVERIFY(embedCover(song, Qt::red));
        CoverCache cache(dir.filePath(u"covers"_s));
        QVERIFY(qRed(cache.load(song, QSize(64, 64)).pixel(10, 10)) > 200);

        QThread::msleep(20); // make sure the modification time moves
        QVERIFY(embedCover(song, Qt::blue));
        const QImage updated = cache.load(song, QSize(64, 64));
        QVERIFY(qBlue(updated.pixel(10, 10)) > 200);
    }

    void remembersMissingCover()
    {
        QTemporaryDir dir;
        const QString song = dir.filePath(u"plain.wav"_s);
        const QString cacheDir = dir.filePath(u"covers"_s);
        QVERIFY(writeSilentWav(song));
        CoverCache cache(cacheDir);
        QVERIFY(cache.load(song, QSize(64, 64)).isNull());
        QCOMPARE(fileCount(cacheDir), 1); // the ".none" marker
        QVERIFY(CoverCache(cacheDir).load(song, QSize(64, 64)).isNull());
    }
};

QTEST_GUILESS_MAIN(TestCoverCache)
#include "tst_covercache.moc"

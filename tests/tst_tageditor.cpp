#include "core/TagEditor.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// Writes one second of 16-bit stereo 44.1 kHz silence as a PCM WAV file.
bool writeSilentWav(const QString &path)
{
    constexpr quint32 kSampleRate = 44100;
    constexpr quint16 kChannels = 2;
    constexpr quint16 kBytesPerFrame = kChannels * 2;
    constexpr quint32 kDataSize = kSampleRate * kBytesPerFrame;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + kDataSize);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << kChannels << kSampleRate << quint32(kSampleRate * kBytesPerFrame)
        << kBytesPerFrame << quint16(16);
    out.writeRawData("data", 4);
    out << kDataSize;
    file.write(QByteArray(kDataSize, '\0'));
    return out.status() == QDataStream::Ok;
}

} // namespace

class TestTagEditor : public QObject
{
    Q_OBJECT

private slots:
    void writesReadsAndClears()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"song.wav"_s);
        QVERIFY(writeSilentWav(path));

        const QVariantMap fields = {{u"title"_s, u"Ofis"_s},          {u"artist"_s, u"Aphrodite's Child"_s},
                                    {u"albumArtist"_s, u"Vangelis"_s}, {u"album"_s, u"666"_s},
                                    {u"genre"_s, u"Prog"_s},           {u"year"_s, u"1972"_s},
                                    {u"track"_s, u"7"_s},              {u"composer"_s, u"Vangelis"_s},
                                    {u"label"_s, u"Vertigo"_s}};
        QVERIFY(TagEditor::write(path, fields).isEmpty());
        QVariantMap read = TagEditor::read(path);
        for (auto it = fields.cbegin(); it != fields.cend(); ++it)
            QCOMPARE(read.value(it.key()).toString(), it.value().toString());
        QVERIFY(!read.value(u"hasCover"_s).toBool());

        // Only the given fields change; an empty one is cleared.
        QVERIFY(TagEditor::write(path, {{u"genre"_s, QString()}, {u"title"_s, u"Ofis (live)"_s}}).isEmpty());
        read = TagEditor::read(path);
        QCOMPARE(read.value(u"genre"_s).toString(), QString());
        QCOMPARE(read.value(u"title"_s).toString(), u"Ofis (live)"_s);
        QCOMPARE(read.value(u"album"_s).toString(), u"666"_s);
    }

    void replacesAndRemovesCover()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"song.wav"_s);
        QVERIFY(writeSilentWav(path));
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        QVERIFY(image.save(&buffer, "JPG"));

        QVERIFY(TagEditor::write(path, {}, TagEditor::Cover::Replace, jpeg).isEmpty());
        QVERIFY(TagEditor::read(path).value(u"hasCover"_s).toBool());
        QVERIFY(TagEditor::write(path, {}, TagEditor::Cover::Remove).isEmpty());
        QVERIFY(!TagEditor::read(path).value(u"hasCover"_s).toBool());
    }
};

QTEST_GUILESS_MAIN(TestTagEditor)
#include "tst_tageditor.moc"

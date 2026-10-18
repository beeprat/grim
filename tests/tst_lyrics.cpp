#include "core/Lyrics.h"

#include <fileref.h>
#include <tpropertymap.h>

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

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

} // namespace

class TestLyrics : public QObject
{
    Q_OBJECT

private slots:
    void parsesSyncedLrc()
    {
        const QList<Lyrics::Line> lines = Lyrics::parse(u"[ar:The Doors]\n"
                                                        "[ti:Unhappy Girl]\n"
                                                        "[00:12.5]Unhappy girl\n"
                                                        "[00:05.00][00:30.45]Chorus line\n"
                                                        "[01:02.123]<01:02.123>Word <01:03.000>timed\n"
                                                        "[01:10]\n"_s);
        QCOMPARE(lines.size(), 5);
        QCOMPARE(lines[0].timeMs, 5'000);
        QCOMPARE(lines[0].text, u"Chorus line"_s);
        QCOMPARE(lines[1].timeMs, 12'500);
        QCOMPARE(lines[2].timeMs, 30'450);
        QCOMPARE(lines[2].text, u"Chorus line"_s);
        QCOMPARE(lines[3].timeMs, 62'123);
        QCOMPARE(lines[3].text, u"Word timed"_s);
        QCOMPARE(lines[4].timeMs, 70'000); // empty line = instrumental gap
        QVERIFY(lines[4].text.isEmpty());
    }

    void appliesOffset()
    {
        // Positive offset makes lyrics appear sooner.
        const QList<Lyrics::Line> lines = Lyrics::parse(u"[offset:+500]\n[00:10.00]a\n[00:00.20]b"_s);
        QCOMPARE(lines[0].timeMs, 0); // clamped, not negative
        QCOMPARE(lines[1].timeMs, 9'500);
    }

    void unsyncedTextStaysPlain()
    {
        const QList<Lyrics::Line> lines = Lyrics::parse(u"\nFirst line\r\n\r\nSecond line\n\n"_s);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].timeMs, -1);
        QCOMPARE(lines[0].text, u"First line"_s);
        QVERIFY(lines[1].text.isEmpty());
        QCOMPARE(lines[2].text, u"Second line"_s);
    }

    void findsCurrentLine()
    {
        const QList<Lyrics::Line> lines = Lyrics::parse(u"[00:05]a\n[00:10]b\n[00:20]c"_s);
        QCOMPARE(Lyrics::lineAt(lines, 0), -1);
        QCOMPARE(Lyrics::lineAt(lines, 5'000), 0);
        QCOMPARE(Lyrics::lineAt(lines, 9'999), 0);
        QCOMPARE(Lyrics::lineAt(lines, 10'000), 1);
        QCOMPARE(Lyrics::lineAt(lines, 999'999), 2);
    }

    void readsTagThenSidecar()
    {
        QTemporaryDir dir;
        const QString song = dir.filePath(u"Glóry.wav"_s);
        QVERIFY(writeSilentWav(song));
        QVERIFY(Lyrics::read(song).isEmpty());

        QFile lrc(dir.filePath(u"Glóry.lrc"_s));
        QVERIFY(lrc.open(QIODevice::WriteOnly));
        lrc.write(u"[00:01.00]från sidecar"_s.toUtf8());
        lrc.close();
        QCOMPARE(Lyrics::read(song), u"[00:01.00]från sidecar"_s);

        {
            const std::wstring path = QDir::toNativeSeparators(song).toStdWString();
            TagLib::FileRef file(path.c_str());
            TagLib::PropertyMap tags = file.properties();
            tags.insert("LYRICS", TagLib::String("[00:02.00]from the tag"));
            file.setProperties(tags);
            QVERIFY(file.save());
        }
        QCOMPARE(Lyrics::read(song), u"[00:02.00]from the tag"_s); // tag wins over sidecar
    }
};

QTEST_GUILESS_MAIN(TestLyrics)
#include "tst_lyrics.moc"

#include "core/CredentialStore.h"
#include "core/FfmpegInstaller.h"
#include "core/SlskdRunner.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QtCore/private/qzipwriter_p.h>

using namespace Qt::StringLiterals;

class TestSlskdRunner : public QObject
{
    Q_OBJECT

private:
    // A small zip shaped like a slskd release: the program plus a folder.
    static QString makeZip(const QString &dir)
    {
        const QString path = dir + u"/slskd.zip"_s;
        QZipWriter writer(path);
        writer.addFile(u"slskd.exe"_s, QByteArray("MZ program"));
        writer.addFile(u"wwwroot/index.html"_s, QByteArray("<html></html>"));
        writer.close();
        return path;
    }

    static QByteArray sha256Of(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
    }

private slots:
    void unpacksAVerifiedZip()
    {
        QTemporaryDir dir;
        const QString zip = makeZip(dir.path());
        const QString destination = dir.path() + u"/0.26.2"_s;
        QDir().mkpath(destination + u"/old"_s); // a previous, broken install is replaced

        QString error;
        QVERIFY2(SlskdRunner::installFromZip(zip, sha256Of(zip), destination, &error), qPrintable(error));
        QFile program(destination + u"/slskd.exe"_s);
        QVERIFY(program.open(QIODevice::ReadOnly));
        QCOMPARE(program.readAll(), QByteArray("MZ program"));
        QVERIFY(QFile::exists(destination + u"/wwwroot/index.html"_s));
        QVERIFY(!QDir(destination + u"/old"_s).exists());
        QVERIFY(!QDir(destination + u".unpacking"_s).exists());
    }

    void refusesAZipWithTheWrongHash()
    {
        QTemporaryDir dir;
        const QString zip = makeZip(dir.path());
        const QString destination = dir.path() + u"/0.26.2"_s;

        QString error;
        QVERIFY(!SlskdRunner::installFromZip(zip, QByteArray(64, 'a'), destination, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QDir(destination).exists()); // nothing unpacked
    }

    // ffmpeg: only bin/ffmpeg.exe and the licence are kept from the build's zip.
    void installsFfmpegFromAVerifiedZip()
    {
        QTemporaryDir dir;
        const QString zip = dir.path() + u"/ffmpeg.zip"_s;
        {
            QZipWriter writer(zip);
            writer.addFile(u"ffmpeg-9.0.2-essentials_build/bin/ffmpeg.exe"_s, QByteArray("MZ ffmpeg"));
            writer.addFile(u"ffmpeg-9.0.2-essentials_build/bin/ffmpeg"_s, QByteArray("ELF ffmpeg"));
            writer.addFile(u"ffmpeg-9.0.2-essentials_build/bin/ffplay.exe"_s, QByteArray("MZ ffplay"));
            writer.addFile(u"ffmpeg-9.0.2-essentials_build/LICENSE"_s, QByteArray("GPL"));
            writer.close();
        }
        const QString destination = dir.path() + u"/9.0.2"_s;
        QString error;
        QVERIFY(!FfmpegInstaller::installFromZip(zip, QByteArray(64, 'a'), destination, &error));
        QVERIFY(!QDir(destination).exists());

        QVERIFY2(FfmpegInstaller::installFromZip(zip, sha256Of(zip), destination, &error), qPrintable(error));
        QVERIFY(QFile::exists(destination + u"/LICENSE.txt"_s));
        QCOMPARE(QDir(destination).entryList(QDir::Files).size(), 2); // no ffplay
    }

    void storesAndRemovesASecret()
    {
        const QString name = u"test-%1"_s.arg(QCoreApplication::applicationPid());
        QVERIFY(CredentialStore::write(name, u"pässwörd 123"_s));
        QCOMPARE(CredentialStore::read(name), u"pässwörd 123"_s);
        CredentialStore::remove(name);
        QCOMPARE(CredentialStore::read(name), QString());
    }
};

QTEST_GUILESS_MAIN(TestSlskdRunner)
#include "tst_slskdrunner.moc"

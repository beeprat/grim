#include "core/FfmpegInstaller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtCore/private/qzipreader_p.h>

using namespace Qt::StringLiterals;

namespace {

// Gyan's "essentials" build (has libmp3lame and the AAC encoder), mirrored on GitHub releases.
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
constexpr auto kSha256 = "60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba";
#else
constexpr auto kSha256 = "";
#endif

#ifdef Q_OS_WIN
const auto kExecutable = "ffmpeg.exe"_L1;
#else
const auto kExecutable = "ffmpeg"_L1;
#endif

QString installDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/ffmpeg/"_s
           + QLatin1StringView(FfmpegInstaller::kVersion);
}

} // namespace

FfmpegInstaller::FfmpegInstaller(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

bool FfmpegInstaller::supported()
{
    return !QLatin1StringView(kSha256).isEmpty();
}

QString FfmpegInstaller::installedPath()
{
    const QString path = installDirectory() + u'/' + kExecutable;
    return QFile::exists(path) ? path : QString();
}

bool FfmpegInstaller::busy() const
{
    return m_busy;
}

double FfmpegInstaller::progress() const
{
    return m_progress;
}

void FfmpegInstaller::install()
{
    if (m_busy)
        return;
    if (!supported()) {
        emit finished(tr("ffmpeg can't be downloaded automatically on this system."));
        return;
    }
    QDir().mkpath(installDirectory() + u"/.."_s);
    const QString zipPath = installDirectory() + u".zip.part"_s;
    auto *file = new QFile(zipPath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        file->deleteLater();
        emit finished(tr("Could not write %1").arg(QDir::toNativeSeparators(zipPath)));
        return;
    }

    const QUrl url(u"https://github.com/GyanD/codexffmpeg/releases/download/%1/ffmpeg-%1-essentials_build.zip"_s
                       .arg(QLatin1StringView(kVersion)));
    QNetworkRequest request(url);
    request.setTransferTimeout(60000);
    QNetworkReply *reply = m_network->get(request); // follows GitHub's https redirect to its file host
    m_busy = true;
    m_progress = 0;
    emit progressChanged();
    // About 110 MB: written to disk as it arrives rather than held in memory.
    connect(reply, &QNetworkReply::readyRead, this, [reply, file] { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_progress = total > 0 ? double(received) / double(total) : 0;
        emit progressChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, zipPath] {
        reply->deleteLater();
        file->write(reply->readAll());
        file->close();
        file->deleteLater();
        QString error;
        if (reply->error() != QNetworkReply::NoError)
            error = tr("Download failed: %1").arg(reply->errorString());
        else
            installFromZip(zipPath, kSha256, installDirectory(), &error);
        QFile::remove(zipPath);
        m_busy = false;
        emit progressChanged();
        emit finished(error);
    });
}

bool FfmpegInstaller::installFromZip(const QString &zipPath, const QByteArray &sha256Hex, const QString &destination,
                                     QString *error)
{
    QFile zip(zipPath);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!zip.open(QIODevice::ReadOnly) || !hash.addData(&zip)) {
        *error = tr("Could not read the ffmpeg download.");
        return false;
    }
    zip.close();
    if (hash.result().toHex() != sha256Hex.toLower()) {
        *error = tr("The ffmpeg download is damaged or not the expected file.");
        return false;
    }

    QZipReader reader(zipPath);
    QString program;
    QString licence;
    for (const QZipReader::FileInfo &entry : reader.fileInfoList()) {
        if (entry.filePath.endsWith(u"/bin/"_s + kExecutable))
            program = entry.filePath;
        else if (entry.filePath.count(u'/') == 1 && entry.filePath.endsWith(u"/LICENSE"_s))
            licence = entry.filePath;
    }
    if (reader.status() != QZipReader::NoError || program.isEmpty()) {
        *error = tr("Could not unpack ffmpeg.");
        return false;
    }

    QDir().mkpath(destination);
    // ffmpeg itself last: its presence is what marks the install as done.
    const auto write = [&](const QString &entry, const QString &name) {
        QSaveFile out(destination + u'/' + name);
        return out.open(QIODevice::WriteOnly) && out.write(reader.fileData(entry)) >= 0 && out.commit();
    };
    if ((!licence.isEmpty() && !write(licence, u"LICENSE.txt"_s)) || !write(program, kExecutable)) {
        *error = tr("Could not install ffmpeg to %1").arg(QDir::toNativeSeparators(destination));
        return false;
    }
#ifndef Q_OS_WIN
    const QString path = destination + u'/' + kExecutable;
    QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner | QFile::ReadOwner);
#endif
    return true;
}

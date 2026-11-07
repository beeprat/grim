#include "core/DownloadConverter.h"

#include <utility>

#include <QFile>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace {
const auto kOriginalFormat = "original"_L1;
}

DownloadConverter::DownloadConverter(QObject *parent)
    : QObject(parent)
    , m_converter(new AudioConverter(this))
    , m_ffmpeg(new FfmpegInstaller(this))
{
    connect(m_converter, &AudioConverter::started, this, [this](const QString &source, const QString &format) {
        emit note(m_conversions.value(source), tr("Converting to %1…").arg(format.toUpper()));
    });
    connect(m_converter, &AudioConverter::finished, this,
            [this](const QString &source, const QString &target, bool ok, const QString &error) {
                const QString downloadId = m_conversions.take(source);
                if (!ok) {
                    emit note(downloadId, tr("Conversion failed: %1").arg(error));
                    emit failed(downloadId, tr("Conversion failed: %1").arg(error));
                    return;
                }
                // Recycle Bin rather than a hard delete, so a bad conversion is recoverable.
                if (!QFile::moveToTrash(source))
                    QFile::remove(source);
                emit ready(target, downloadId,
                           tr("Converted to %1 and added to the library").arg(QFileInfo(target).suffix().toUpper()));
            });
    connect(m_ffmpeg, &FfmpegInstaller::progressChanged, this, [this] {
        if (!m_ffmpeg->busy())
            return;
        const QString text = tr("Downloading ffmpeg… %1 %").arg(qRound(m_ffmpeg->progress() * 100));
        for (const QString &downloadId : std::as_const(m_waiting))
            emit note(downloadId, text);
    });
    connect(m_ffmpeg, &FfmpegInstaller::finished, this, [this](const QString &error) {
        if (error.isEmpty()) {
            releaseWaiting();
            return;
        }
        emit installFailed(error);
        for (const QString &downloadId : std::as_const(m_waiting))
            emit note(downloadId, tr("Waiting for ffmpeg…"));
        emit ffmpegNeeded(); // try again or keep the originals
    });
}

void DownloadConverter::setFormat(const QString &format)
{
    m_format = format;
}

bool DownloadConverter::isValidFormat(const QString &format)
{
    return format == kOriginalFormat || AudioConverter::formats().contains(format);
}

bool DownloadConverter::canDownloadFfmpeg()
{
    return FfmpegInstaller::supported();
}

void DownloadConverter::process(const QString &path, const QString &downloadId)
{
    // Kept as downloaded: "original" chosen, or already in the chosen format (no re-encode).
    if (m_format == kOriginalFormat || QFileInfo(path).suffix().compare(m_format, Qt::CaseInsensitive) == 0) {
        emit ready(path, downloadId, tr("Added to the library"));
        return;
    }
    if (AudioConverter::ffmpegPath().isEmpty()) {
        if (m_declined) {
            emit ready(path, downloadId, tr("Added to the library as downloaded (no ffmpeg to convert it)"));
            return;
        }
        // One question for the whole batch: later downloads join the ones already waiting.
        const bool ask = m_waiting.isEmpty() && !m_ffmpeg->busy();
        m_waiting.insert(path, downloadId);
        emit note(downloadId, tr("Waiting for ffmpeg…"));
        if (ask)
            emit ffmpegNeeded();
        return;
    }
    m_conversions.insert(path, downloadId);
    emit note(downloadId, tr("Waiting to convert…"));
    m_converter->setFormat(m_format);
    m_converter->convert(path);
}

void DownloadConverter::installFfmpeg()
{
    if (!AudioConverter::ffmpegPath().isEmpty())
        releaseWaiting(); // installed meanwhile (Linux: from the system's packages)
    else if (FfmpegInstaller::supported())
        m_ffmpeg->install();
    else if (!m_waiting.isEmpty())
        emit ffmpegNeeded(); // still missing: ask again
}

void DownloadConverter::keepOriginals()
{
    m_declined = true;
    releaseWaiting();
}

void DownloadConverter::releaseWaiting()
{
    const QHash<QString, QString> waiting = std::exchange(m_waiting, {});
    for (auto it = waiting.cbegin(); it != waiting.cend(); ++it)
        process(it.key(), it.value());
}

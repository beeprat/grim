#pragma once

#include "core/AudioConverter.h"
#include "core/FfmpegInstaller.h"

#include <QHash>
#include <QObject>
#include <QString>

// Turns finished Soulseek downloads into the chosen format before they go into the library: converts
// with ffmpeg, holds them while ffmpeg is missing (asking once per batch), and downloads ffmpeg where
// supported. Reports through signals keyed by the slskd download id; SlskdController does the import.
class DownloadConverter : public QObject
{
    Q_OBJECT

public:
    explicit DownloadConverter(QObject *parent = nullptr);

    // "original" (moved as downloaded, no ffmpeg needed) or an AudioConverter format ("m4a", "mp3", "flac").
    void setFormat(const QString &format);
    static bool isValidFormat(const QString &format);
    static bool canDownloadFfmpeg();

    // Converts, holds (ffmpeg missing) or passes on one finished download at `path`.
    void process(const QString &path, const QString &downloadId);
    // Answers to ffmpegNeeded: convert the waiting downloads (downloading ffmpeg first where supported),
    // or pass them on as downloaded, as later ones this session.
    void installFfmpeg();
    void keepOriginals();

signals:
    // Progress text for the download's row.
    void note(const QString &downloadId, const QString &text);
    // `path` is ready for the library (converted or kept as is); `doneNote` describes what happened.
    void ready(const QString &path, const QString &downloadId, const QString &doneNote);
    void failed(const QString &downloadId, const QString &reason);
    // A download should be converted but ffmpeg is missing; it waits for installFfmpeg() or keepOriginals().
    void ffmpegNeeded();
    void installFailed(const QString &error);

private:
    // Runs the downloads held for ffmpeg through process() again.
    void releaseWaiting();

    AudioConverter *m_converter;
    FfmpegInstaller *m_ffmpeg;
    QString m_format = QStringLiteral("m4a");
    QHash<QString, QString> m_waiting;     // source path -> download id, held for ffmpeg
    QHash<QString, QString> m_conversions; // source path -> download id
    bool m_declined = false;               // keepOriginals(): no more asking this session
};

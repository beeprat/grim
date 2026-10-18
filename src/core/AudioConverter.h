#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

// Converts audio files next to the source, one at a time, with ffmpeg (from PATH): to M4A
// (AAC 320 kbps, the default), MP3 (320 kbps) or FLAC. Tags and cover art are copied with
// TagLib afterwards, because ffmpeg's muxers drop freeform tags such as ReplayGain.
// The source file is kept.
class AudioConverter : public QObject
{
    Q_OBJECT

public:
    explicit AudioConverter(QObject *parent = nullptr);
    // Stops a running ffmpeg and deletes its unfinished output.
    ~AudioConverter() override;

    // Empty if ffmpeg is not on PATH.
    static QString ffmpegPath();
    // Target formats by file extension: "m4a", "mp3", "flac".
    static QStringList formats();
    static QString targetPathFor(const QString &sourcePath, const QString &format);

    // Format of conversions queued from now on (one of formats()); queued ones keep theirs.
    void setFormat(const QString &format);
    QString format() const;

    // Queues a conversion. If the target already exists, finished() is emitted at once with ok = true.
    void convert(const QString &sourcePath);

signals:
    void started(const QString &sourcePath, const QString &format);
    void finished(const QString &sourcePath, const QString &targetPath, bool ok, const QString &error);

private:
    void startNext();
    void finishCurrent(bool ok, const QString &error);

    struct Job
    {
        QString source;
        QString format;
    };

    QProcess *m_process;
    QString m_format = QStringLiteral("m4a");
    QList<Job> m_queue;
    QString m_current;       // source being converted, empty when idle
    QString m_currentFormat; // its target format
    QString m_partialPath; // ffmpeg output before tags are copied and it is renamed
};

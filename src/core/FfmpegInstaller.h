#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

// grim.'s own copy of ffmpeg for converting downloads, so the user never installs it or edits PATH.
// install() downloads the build pinned below (verified by SHA-256) and keeps only ffmpeg.exe and its
// licence, in the local data folder. Windows x64 only: elsewhere ffmpeg comes from the system's packages.
class FfmpegInstaller : public QObject
{
    Q_OBJECT

public:
    static constexpr auto kVersion = "9.0.2";

    explicit FfmpegInstaller(QObject *parent = nullptr);

    // A pinned build exists for this system.
    static bool supported();
    // The installed ffmpeg, empty if not installed.
    static QString installedPath();

    bool busy() const;
    double progress() const; // 0..1 while busy

    void install();

    // Checks `zipPath` against `sha256Hex` and copies ffmpeg(.exe) and LICENSE from its bin/ folder into
    // `destination`. Used by install(); public for the test.
    static bool installFromZip(const QString &zipPath, const QByteArray &sha256Hex, const QString &destination,
                               QString *error);

signals:
    void progressChanged();
    void finished(const QString &error); // empty = installed

private:
    QNetworkAccessManager *m_network;
    bool m_busy = false;
    double m_progress = 0;
};

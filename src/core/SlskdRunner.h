#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QProcess;
class QTimer;

// Runs a private copy of slskd for grim. ("Run slskd for me" in Settings), so the user never installs or
// configures it. install() downloads the official release pinned below (verified by SHA-256) into the local
// data folder; start() runs it hidden, with its own data folder, listening on 127.0.0.1:kPort only and
// requiring the API key grim. passes. The process ends with grim. (and with a crash of grim.: a job object on
// Windows, PR_SET_PDEATHSIG on Linux). An unexpected exit is retried a few times, then reported.
class SlskdRunner : public QObject
{
    Q_OBJECT

public:
    enum State {
        NotInstalled,
        Downloading,
        Stopped, // installed, not running
        Running, // process started; slskd answers a few seconds later
        Failed,  // see errorText()
    };
    Q_ENUM(State)

    static constexpr int kPort = 5031; // not slskd's 5030, so a slskd the user runs themselves can coexist
    static constexpr auto kVersion = "0.26.2";

    explicit SlskdRunner(QObject *parent = nullptr);
    ~SlskdRunner() override;

    State state() const;
    double progress() const; // 0..1 while Downloading
    QString errorText() const;
    QString serverUrl() const;
    // Where slskd keeps its settings, logs and downloads.
    QString dataDirectory() const;

    void install();
    // Starts (or restarts) slskd logged in as `username`. `apiKey`: 16..255 characters.
    void start(const QString &username, const QString &password, const QString &apiKey);
    // slskd's own web page at serverUrl(), behind this login; an empty username keeps it off. Applies at
    // the next start.
    void setWebLogin(const QString &username, const QString &password);
    void stop();

    // Checks `zipPath` against `sha256Hex` and unpacks it into `destination` (replaced). Used by install();
    // public for the test.
    static bool installFromZip(const QString &zipPath, const QByteArray &sha256Hex, const QString &destination,
                               QString *error);

signals:
    void stateChanged();

private:
    void setState(State state, const QString &error = {});
    QString installDirectory() const;
    QString executable() const;
    void launch();

    QNetworkAccessManager *m_network;
    QProcess *m_process = nullptr;
    QTimer *m_retryTimer;
    State m_state = NotInstalled;
    double m_progress = 0;
    QString m_errorText;
    QString m_username;
    QString m_password;
    QString m_apiKey;
    QString m_webUsername;
    QString m_webPassword;
    int m_retries = 0;
    void *m_job = nullptr; // Windows job object HANDLE
};

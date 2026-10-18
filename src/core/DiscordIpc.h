#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QLocalSocket;
class QTimer;

// Discord Rich Presence over the desktop client's local IPC pipe (\\.\pipe\discord-ipc-N):
// 8-byte header (opcode, length, little-endian) + JSON. Reconnects on its own while
// started, and resends the last activity after a reconnect.
class DiscordIpc : public QObject
{
    Q_OBJECT

public:
    explicit DiscordIpc(QObject *parent = nullptr);
    ~DiscordIpc() override;

    // For tests; by default the desktop client's pipe (Windows) or socket (Linux) is found (0..9 are tried).
    void setPipeBaseName(const QString &baseName);

    void start(const QString &clientId);
    void stop();
    // Handshake done; activities are shown.
    bool isReady() const;

    // An empty object clears the presence.
    void setActivity(const QJsonObject &activity);

signals:
    void readyChanged();
    // Discord answered a command with an error (text from Discord).
    void errorOccurred(const QString &message);

private:
    enum Op { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

    void tryNextPipe();
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void send(int op, const QJsonObject &payload);
    void sendActivity();
    void setReady(bool ready);

    QLocalSocket *m_socket;
    QTimer *m_retry;
    QString m_baseName;
    QString m_clientId;
    int m_pipeIndex = 0;
    bool m_ready = false;
    QByteArray m_buffer;
    QJsonObject m_activity;
    int m_nonce = 0;
};

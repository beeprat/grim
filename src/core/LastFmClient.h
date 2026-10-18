#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

// Last.fm Scrobbling API 2.0 (desktop auth flow) with a persistent queue: scrobbles are
// written to `queueFile` first and removed only once Last.fm accepted them, so plays made
// offline or during an outage are sent later.
class LastFmClient : public QObject
{
    Q_OBJECT

public:
    struct Scrobble
    {
        QString artist;
        QString title;
        QString album;
        qint64 durationMs = 0;
        qint64 timestamp = 0; // Unix seconds, UTC, when the track started
    };

    explicit LastFmClient(const QString &queueFile, QObject *parent = nullptr);

    // For tests; defaults to https://ws.audioscrobbler.com/2.0/.
    void setBaseUrl(const QUrl &baseUrl);
    void setApiKey(const QString &apiKey, const QString &secret);
    void setSessionKey(const QString &sessionKey);
    bool hasSession() const;

    // md5 of the parameters sorted by name as name+value, then the secret ("format" excluded).
    static QString signature(const QMap<QString, QString> &params, const QString &secret);
    // Page where the user approves the token.
    static QUrl authPage(const QString &apiKey, const QString &token);

    // Each callback runs exactly once; empty values mean failure (errorOccurred has the reason).
    void requestToken(std::function<void(const QString &token)> onToken);
    void requestSession(const QString &token,
                        std::function<void(const QString &user, const QString &sessionKey)> onSession);

    void updateNowPlaying(const Scrobble &track);
    // Stores the scrobble, then tries to send everything waiting.
    void enqueue(const Scrobble &scrobble);
    void flush();
    int pendingCount() const;

signals:
    void errorOccurred(const QString &message);
    // Last.fm no longer accepts the session key (access revoked); the queue is kept.
    void sessionRejected();
    void pendingCountChanged();

private:
    using Params = QMap<QString, QString>;
    // error: 0 = ok, -1 = no usable answer (network), else Last.fm's error code.
    void call(Params params, bool signedWithSession,
              std::function<void(int error, const QJsonObject &answer)> onDone);
    void loadQueue();
    void saveQueue() const;

    QNetworkAccessManager *m_network;
    QString m_queueFile;
    QUrl m_baseUrl;
    QString m_apiKey;
    QString m_secret;
    QString m_sessionKey;
    QList<Scrobble> m_queue;
    bool m_flushing = false;
};

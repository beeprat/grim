#pragma once

#include "core/LastFmClient.h"
#include "models/Track.h"

#include <QDateTime>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

// Last.fm scrobbling for the Settings page: the browser approval flow and the scrobbles fed by
// PlayerController. The user's own API key and secret come from a text file (lastfm.txt in the
// data folder), not the Settings page. One instance, created in main.cpp before the QML engine.
class LastFmController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Empty while no account is connected.
    Q_PROPERTY(QString userName READ userName NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool awaitingApproval READ awaitingApproval NOTIFY awaitingApprovalChanged FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged FINAL)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY pendingCountChanged FINAL)

public:
    // Listening time after which a play is scrobbled.
    static constexpr qint64 kScrobbleAfterMs = 15000;

    // keysFile: lastfm.txt with `api_key = ...` and `shared_secret = ...`; created on first start (from
    // the key and secret older versions kept in the registry) and re-read on "Connect account".
    LastFmController(const QString &queueFile, const QString &keysFile, QObject *parent = nullptr);
    ~LastFmController() override;

    static LastFmController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    QString userName() const;
    bool awaitingApproval() const;
    QString errorText() const;
    int pendingCount() const;

    // Re-reads the key file, gets a token and opens the approval page in the browser.
    Q_INVOKABLE void connectAccount();
    // After the user approved in the browser: exchanges the token for a session.
    Q_INVOKABLE void finishConnect();
    Q_INVOKABLE void disconnectAccount();

    void trackStarted(const Track &track);
    void trackListened(const Track &track, const QDateTime &startedUtc);

signals:
    void settingsChanged();
    void awaitingApprovalChanged();
    void errorTextChanged();
    void pendingCountChanged();

private:
    void setErrorText(const QString &text);
    void setSession(const QString &user, const QString &sessionKey);
    void loadKeys();

    static inline LastFmController *s_instance = nullptr;
    LastFmClient *m_client;
    QString m_keysFile;
    QString m_apiKey;
    QString m_apiSecret;
    QString m_userName;
    QString m_token; // waiting for browser approval
    QString m_errorText;
};

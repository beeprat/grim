#pragma once

#include "core/CoverCache.h"
#include "core/DiscordIpc.h"
#include "core/ImgurUploader.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

class PlayerController;
class QJSEngine;
class QQmlEngine;
class QTimer;

// Discord Rich Presence for the playing track: "Listening to <artist>" with title,
// album, progress and the album cover. Covers go to imgur anonymously, once per unique
// image. Off until enabled in Settings. One instance, created in main.cpp.
class DiscordController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged FINAL)
    // The IDs come from a text file in the data folder, not from the Settings page.
    Q_PROPERTY(QString keysFile READ keysFile CONSTANT FINAL)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged FINAL)

public:
    // keysFile: discord.txt with `application_id = ...` and `imgur_client_id = ...`; created on first start
    // (from the IDs older versions kept in the registry) and re-read whenever presence is turned on.
    DiscordController(PlayerController &player, const QString &coverCacheDir, const QString &uploadCacheFile,
                      const QString &keysFile, QObject *parent = nullptr);
    ~DiscordController() override;

    static DiscordController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    bool enabled() const;
    void setEnabled(bool enabled);
    QString keysFile() const;
    bool connected() const;
    QString errorText() const;

signals:
    void settingsChanged();
    void connectedChanged();
    void errorTextChanged();

private:
    void restart();
    void loadKeys();
    void onTrackChanged();
    void schedule();
    void publish();
    void setErrorText(const QString &text);

    static inline DiscordController *s_instance = nullptr;
    PlayerController &m_player;
    DiscordIpc *m_ipc;
    ImgurUploader *m_imgur;
    CoverCache m_covers;
    QTimer *m_debounce;
    bool m_enabled = false;
    QString m_keysFile;
    QString m_applicationId;
    QString m_imgurClientId;
    QString m_errorText;
    QString m_coverUrl;   // imgur link of the current track's cover, empty until known
    QString m_coverKey;   // content key of the current track's cover
    qint64 m_sentStartMs = 0; // track start time in the last presence sent (epoch ms)
};

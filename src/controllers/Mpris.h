#pragma once

#include "core/CoverCache.h"

#include <QObject>
#include <QPointer>
#include <QWindow>

class PlayerController;

// Linux counterpart of MediaKeys: publishes the player on the session bus as an MPRIS 2 player
// (org.mpris.MediaPlayer2.grim). Desktops route the keyboard media keys to it and show it in their
// sound/notification menus (title, artist, album, length, play/pause/next/previous). Not exposed to QML.
class Mpris : public QObject
{
    Q_OBJECT

public:
    // coverCacheDir: where cover thumbnails are kept (shared with the other cover caches).
    Mpris(PlayerController &player, const QString &coverCacheDir, QObject *parent = nullptr);
    ~Mpris() override;

    // The window "Raise" brings to the front.
    void setWindow(QWindow *window);

    PlayerController &player() const { return m_player; }
    QWindow *window() const { return m_window; }
    // file:// URL of the current track's cover as a JPEG, or empty.
    QString artUrl();

private:
    PlayerController &m_player;
    QPointer<QWindow> m_window;
    CoverCache m_covers;
};

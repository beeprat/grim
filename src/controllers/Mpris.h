#pragma once

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
    explicit Mpris(PlayerController &player, QObject *parent = nullptr);
    ~Mpris() override;

    // The window "Raise" brings to the front.
    void setWindow(QWindow *window);

    PlayerController &player() const { return m_player; }
    QWindow *window() const { return m_window; }

private:
    PlayerController &m_player;
    QPointer<QWindow> m_window;
};

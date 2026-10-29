#pragma once

#include <QMenu>
#include <QObject>
#include <QPointer>

class PlayerController;
class QSystemTrayIcon;
class QAction;
class QToolButton;
class QWindow;
class SettingsController;

// System tray icon. With "minimize to tray" on, minimizing hides the window
// to the tray; clicking the icon brings it back. Its right-click menu starts with the
// song that is playing, then playback controls. Not exposed to QML.
class TrayController : public QObject
{
    Q_OBJECT

public:
    TrayController(SettingsController &settings, PlayerController &player, const QIcon &icon,
                   QObject *parent = nullptr);

    void setWindow(QWindow *window);
    // Shows the window from the tray or the taskbar (keeping it maximized if it was) and brings it to the front.
    void restoreWindow();

private:
    void updateNowPlaying();
    QIcon menuIcon(const QString &name) const;

    SettingsController &m_settings;
    PlayerController &m_player;
    // Windows: the menu is rows of widgets. Elsewhere tray menus are sent to the desktop over
    // D-Bus, which shows only plain actions (widget rows came up blank), so it uses these instead.
    QToolButton *m_nowPlaying = nullptr;
    QToolButton *m_shuffle = nullptr;
    QToolButton *m_previous = nullptr;
    QToolButton *m_playPause = nullptr;
    QToolButton *m_next = nullptr;
    QToolButton *m_repeat = nullptr;
    QAction *m_nowPlayingAction = nullptr;
    QAction *m_shuffleAction = nullptr;
    QAction *m_previousAction = nullptr;
    QAction *m_playPauseAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_repeatAction = nullptr;
    QSystemTrayIcon *m_tray;
    QMenu m_menu; // QMenu is a widget; it cannot have a QObject parent
    QPointer<QWindow> m_window;
};

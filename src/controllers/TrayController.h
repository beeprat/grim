#pragma once

#include <QMenu>
#include <QObject>
#include <QPointer>

class PlayerController;
class QAction;
class QSystemTrayIcon;
class QWindow;
class SettingsController;

// System tray icon. With "minimize to tray" on, minimizing hides the window
// to the tray; clicking the icon brings it back. Its right-click menu starts with the
// song that is playing. Not exposed to QML.
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

    SettingsController &m_settings;
    PlayerController &m_player;
    QAction *m_nowPlaying;
    QSystemTrayIcon *m_tray;
    QMenu m_menu; // QMenu is a widget; it cannot have a QObject parent
    QPointer<QWindow> m_window;
};

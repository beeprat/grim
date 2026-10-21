#pragma once

#include <QAbstractNativeEventFilter>
#include <QList>
#include <QObject>

#include <qt_windows.h>

class PlayerController;
class QWindow;
struct ITaskbarList3;
struct THUMBBUTTON;

// Keyboard media keys (play/pause, next, previous, stop) work while the app is in the
// background: they are registered as global Windows hotkeys on the main window.
// A key another program already owns stays with that program; while our window has
// focus, the QML shortcuts in Main.qml still handle it. It also adds previous / play-pause / next
// buttons to the taskbar thumbnail (shown when hovering the taskbar icon). Not exposed to QML.
class MediaKeys : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit MediaKeys(PlayerController &player, QObject *parent = nullptr);
    ~MediaKeys() override;

    void registerFor(QWindow *window);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    bool trigger(int id);
    void addThumbButtons();
    void updateThumbButtons();
    void fillThumbButtons(THUMBBUTTON (&buttons)[3]) const;

    PlayerController &m_player;
    quintptr m_window = 0; // HWND the hotkeys are registered on
    QList<int> m_registered;
    UINT m_taskbarButtonCreated = 0; // sent again whenever Explorer recreates our taskbar button
    ITaskbarList3 *m_taskbar = nullptr;
    bool m_thumbButtonsAdded = false;
    HICON m_previousIcon = nullptr, m_playIcon = nullptr, m_pauseIcon = nullptr, m_nextIcon = nullptr;
};

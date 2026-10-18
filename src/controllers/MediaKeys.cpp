#include "controllers/MediaKeys.h"

#include "controllers/PlayerController.h"

#include <QCoreApplication>
#include <QWindow>
#include <QtLogging>

#include <windows.h>

namespace {

// Hotkey ids are ours to choose; the virtual-key codes are Windows' media keys.
enum HotkeyId { PlayPause = 1, Next, Previous, Stop };

struct Hotkey
{
    HotkeyId id;
    UINT virtualKey;
    const char *name;
};

constexpr Hotkey kHotkeys[] = {
    {PlayPause, VK_MEDIA_PLAY_PAUSE, "Play/Pause"},
    {Next, VK_MEDIA_NEXT_TRACK, "Next track"},
    {Previous, VK_MEDIA_PREV_TRACK, "Previous track"},
    {Stop, VK_MEDIA_STOP, "Stop"},
};

} // namespace

MediaKeys::MediaKeys(PlayerController &player, QObject *parent)
    : QObject(parent)
    , m_player(player)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

MediaKeys::~MediaKeys()
{
    QCoreApplication::instance()->removeNativeEventFilter(this);
    for (const int id : std::as_const(m_registered))
        UnregisterHotKey(reinterpret_cast<HWND>(m_window), id);
}

void MediaKeys::registerFor(QWindow *window)
{
    if (!window || m_window != 0)
        return;
    m_window = static_cast<quintptr>(window->winId());
    for (const Hotkey &hotkey : kHotkeys) {
        if (RegisterHotKey(reinterpret_cast<HWND>(m_window), hotkey.id, MOD_NOREPEAT, hotkey.virtualKey))
            m_registered.append(hotkey.id);
        else // e.g. another player holds it; the in-window shortcut still works
            qInfo() << "MediaKeys: media key not available globally:" << hotkey.name;
    }
}

bool MediaKeys::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message != WM_HOTKEY || reinterpret_cast<quintptr>(msg->hwnd) != m_window)
        return false;

    switch (static_cast<int>(msg->wParam)) {
    case PlayPause:
        m_player.togglePlay();
        return true;
    case Next:
        m_player.next();
        return true;
    case Previous:
        m_player.previous();
        return true;
    case Stop:
        if (m_player.playing())
            m_player.togglePlay();
        return true;
    default:
        return false;
    }
}

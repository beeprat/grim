#include "controllers/MediaKeys.h"

#include "controllers/PlayerController.h"

#include <QCoreApplication>
#include <QIcon>
#include <QImage>
#include <QWindow>
#include <QtLogging>

#include <windows.h>
#include <shobjidl.h>

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

HICON thumbIcon(const char *name, int side)
{
    const QIcon icon(QStringLiteral(":/qt/qml/MusicPlayer/qml/icons/%1.svg").arg(QLatin1String(name)));
    return icon.pixmap(QSize(side, side), 1.0).toImage().toHICON();
}

} // namespace

MediaKeys::MediaKeys(PlayerController &player, QObject *parent)
    : QObject(parent)
    , m_player(player)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(&m_player, &PlayerController::playingChanged, this, &MediaKeys::updateThumbButtons);
    connect(&m_player, &PlayerController::trackChanged, this, &MediaKeys::updateThumbButtons);
}

MediaKeys::~MediaKeys()
{
    QCoreApplication::instance()->removeNativeEventFilter(this);
    for (const int id : std::as_const(m_registered))
        UnregisterHotKey(reinterpret_cast<HWND>(m_window), id);
    if (m_taskbar)
        m_taskbar->Release();
    for (HICON icon : {m_previousIcon, m_playIcon, m_pauseIcon, m_nextIcon})
        if (icon)
            DestroyIcon(icon);
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
    m_taskbarButtonCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
    addThumbButtons(); // the window is already shown, so its taskbar button may exist before we listen
}

void MediaKeys::addThumbButtons()
{
    const HWND hwnd = reinterpret_cast<HWND>(m_window);
    if (!m_taskbar) {
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_taskbar))))
            return;
        if (FAILED(m_taskbar->HrInit())) {
            m_taskbar->Release();
            m_taskbar = nullptr;
            return;
        }
        const int side = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(hwnd));
        m_previousIcon = thumbIcon("previous", side);
        m_playIcon = thumbIcon("play", side);
        m_pauseIcon = thumbIcon("pause", side);
        m_nextIcon = thumbIcon("next", side);
    }
    // A new taskbar button (first show, back from the tray, Explorer restart) has no buttons yet.
    THUMBBUTTON buttons[3];
    fillThumbButtons(buttons);
    m_thumbButtonsAdded = SUCCEEDED(m_taskbar->ThumbBarAddButtons(hwnd, 3, buttons));
}

void MediaKeys::updateThumbButtons()
{
    if (!m_thumbButtonsAdded)
        return;
    THUMBBUTTON buttons[3];
    fillThumbButtons(buttons);
    m_taskbar->ThumbBarUpdateButtons(reinterpret_cast<HWND>(m_window), 3, buttons);
}

void MediaKeys::fillThumbButtons(THUMBBUTTON (&buttons)[3]) const
{
    const bool playing = m_player.playing();
    const QString tips[3] = {tr("Previous"), playing ? tr("Pause") : tr("Play"), tr("Next")};
    const HICON icons[3] = {m_previousIcon, playing ? m_pauseIcon : m_playIcon, m_nextIcon};
    const HotkeyId ids[3] = {Previous, PlayPause, Next};
    for (int i = 0; i < 3; ++i) {
        buttons[i] = {};
        buttons[i].iId = ids[i];
        buttons[i].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        buttons[i].hIcon = icons[i];
        buttons[i].dwFlags = m_player.hasTrack() ? THBF_ENABLED : THBF_DISABLED;
        wcsncpy_s(buttons[i].szTip, reinterpret_cast<const wchar_t *>(tips[i].utf16()), _TRUNCATE);
    }
}

bool MediaKeys::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (reinterpret_cast<quintptr>(msg->hwnd) != m_window)
        return false;
    if (m_taskbarButtonCreated != 0 && msg->message == m_taskbarButtonCreated) {
        addThumbButtons();
        return false;
    }
    if (msg->message == WM_COMMAND && HIWORD(msg->wParam) == THBN_CLICKED)
        return trigger(LOWORD(msg->wParam));
    if (msg->message == WM_HOTKEY)
        return trigger(static_cast<int>(msg->wParam));
    return false;
}

bool MediaKeys::trigger(int id)
{
    switch (id) {
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

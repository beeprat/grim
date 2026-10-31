#include "controllers/WindowFrame.h"

#include <QCoreApplication>
#include <QQuickWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

WindowFrame::WindowFrame(QObject *parent)
    : QObject(parent)
{
}

WindowFrame::~WindowFrame()
{
#ifdef Q_OS_WIN
    if (m_hwnd)
        QCoreApplication::instance()->removeNativeEventFilter(this);
#endif
}

void WindowFrame::attach(QQuickWindow *window)
{
#ifdef Q_OS_WIN
    m_window = window;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    m_hwnd = reinterpret_cast<quintptr>(hwnd);
    QCoreApplication::instance()->installNativeEventFilter(this);
    // Caption, resizable frame and maximize box make the window snappable: dragging it by the title bar
    // (startSystemMove) to a screen edge or corner snaps it like any window. WM_NCCALCSIZE below keeps
    // the frame from being drawn (the whole window stays client area).
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
#else
    Q_UNUSED(window)
#endif
}

bool WindowFrame::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG")
        return false;
    MSG *msg = static_cast<MSG *>(message);
    if (reinterpret_cast<quintptr>(msg->hwnd) != m_hwnd || !m_window)
        return false;

    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        // No frame: the client area is the whole window. Maximized, Windows places the window a
        // frame's width past the screen edges; keep the client area on the work area.
        if (IsZoomed(msg->hwnd)) {
            auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
                params->rgrc[0] = monitor.rcWork;
        }
        *result = 0;
        return true;
    }
    return false;
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
}

#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>

class QQuickWindow;

// Windows snapping for the frameless main window: it gets the styles Windows needs to snap a window
// (caption, resizable frame, maximize box) without drawing a frame, so dragging it to a screen edge or
// corner snaps it. Other platforms: does nothing. Not exposed to QML.
class WindowFrame : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit WindowFrame(QObject *parent = nullptr);
    ~WindowFrame() override;

    void attach(QQuickWindow *window);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    QPointer<QQuickWindow> m_window;
    quintptr m_hwnd = 0;
};

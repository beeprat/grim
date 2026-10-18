#pragma once

#include <QAbstractNativeEventFilter>
#include <QList>
#include <QObject>

class PlayerController;
class QWindow;

// Keyboard media keys (play/pause, next, previous, stop) work while the app is in the
// background: they are registered as global Windows hotkeys on the main window.
// A key another program already owns stays with that program; while our window has
// focus, the QML shortcuts in Main.qml still handle it. Not exposed to QML.
class MediaKeys : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit MediaKeys(PlayerController &player, QObject *parent = nullptr);
    ~MediaKeys() override;

    void registerFor(QWindow *window);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    PlayerController &m_player;
    quintptr m_window = 0; // HWND the hotkeys are registered on
    QList<int> m_registered;
};

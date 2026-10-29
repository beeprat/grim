#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <thread>

class QMediaPlayer;

// A muted, looping video for the window background (Settings → Appearance → Background), drawn
// as small frames since the background is blurred anyway. Linux decodes on its own thread with
// FFmpeg (libdav1d for AV1: Qt Multimedia's FFmpeg decodes AV1 only on GPUs that support it, and
// many webm files are AV1); Windows plays it with Qt Multimedia.
class WallpaperVideo : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    // Image.Stretch, Image.PreserveAspectFit or Image.PreserveAspectCrop.
    Q_PROPERTY(int fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged FINAL)
    // 0..1, applied to every frame (core/WallpaperBlur).
    Q_PROPERTY(qreal blur READ blur WRITE setBlur NOTIFY blurChanged FINAL)
    // False pauses decoding (window minimized or in the tray).
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
    // A first frame was shown.
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)

public:
    explicit WallpaperVideo(QQuickItem *parent = nullptr);
    ~WallpaperVideo() override;

    QUrl source() const;
    void setSource(const QUrl &source);
    int fillMode() const;
    void setFillMode(int mode);
    bool ready() const;
    qreal blur() const;
    bool active() const;
    void setActive(bool active);
    void setBlur(qreal blur);

signals:
    void sourceChanged();
    void fillModeChanged();
    void readyChanged();
    void blurChanged();
    void activeChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    void start();
    void stop();
    void decode(const QString &path); // the decoding thread (Linux)
    void deliver(QImage frame);       // any thread

    QUrl m_source;
    int m_fillMode = 0;
    bool m_ready = false;
    std::atomic<qreal> m_blur{1};
    std::atomic_bool m_active{true};
    QMediaPlayer *m_player = nullptr; // Windows
    std::thread m_thread;
    std::atomic_bool m_stop{false};
    QMutex m_mutex;
    QImage m_frame; // newest frame, guarded by m_mutex
    bool m_newFrame = false;
};

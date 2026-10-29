#include "WallpaperVideo.h"

#include "core/WallpaperBlur.h"

#include <QElapsedTimer>
#include <QFile>
#include <QMetaObject>
#include <QScopeGuard>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QtLogging>

#include <algorithm>
#include <chrono>

#ifdef Q_OS_WIN
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#else
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}
#endif

namespace {

// Frames are converted at about this width: blurred anyway, and converting (or blurring) every
// pixel of a 1080p frame took a whole CPU core. Sharp (no blur) videos get twice the width.
constexpr int kBlurredWidth = 640;

int frameWidth(qreal blur)
{
    return blur > 0 ? kBlurredWidth : 2 * kBlurredWidth;
}

enum FillMode { Stretch = 0, PreserveAspectFit = 1, PreserveAspectCrop = 2 }; // Image.fillMode values

#ifndef Q_OS_WIN

// Planar YUV 4:2:0 / 4:2:2 / 4:4:4, 8 or 10 bit, to RGB (BT.709, limited range), every step-th pixel.
QImage toImage(const AVFrame *frame, int maxWidth)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    if (!desc || !(desc->flags & AV_PIX_FMT_FLAG_PLANAR) || (desc->flags & AV_PIX_FMT_FLAG_RGB)
        || desc->nb_components < 3 || desc->comp[0].depth > 10)
        return {};
    const int step = std::max(1, frame->width / maxWidth);
    const int width = frame->width / step;
    const int height = frame->height / step;
    const bool wide = desc->comp[0].depth > 8;
    const int shift = desc->comp[0].depth - 8;
    const auto sample = [&](int plane, int x, int y) {
        const uint8_t *row = frame->data[plane] + static_cast<ptrdiff_t>(y) * frame->linesize[plane];
        return wide ? reinterpret_cast<const uint16_t *>(row)[x] >> shift : row[x];
    };
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
        const int sy = y * step;
        const int cy = sy >> desc->log2_chroma_h;
        for (int x = 0; x < width; ++x) {
            const int sx = x * step;
            const int cx = sx >> desc->log2_chroma_w;
            const float l = 1.164f * (sample(0, sx, sy) - 16);
            const float u = sample(1, cx, cy) - 128.0f;
            const float v = sample(2, cx, cy) - 128.0f;
            const auto clamp = [](float c) { return std::clamp(static_cast<int>(c), 0, 255); };
            out[x] = qRgb(clamp(l + 1.793f * v), clamp(l - 0.213f * u - 0.533f * v), clamp(l + 2.112f * u));
        }
    }
    return image;
}
#endif

} // namespace

WallpaperVideo::WallpaperVideo(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

WallpaperVideo::~WallpaperVideo()
{
    stop();
}

QUrl WallpaperVideo::source() const
{
    return m_source;
}

void WallpaperVideo::setSource(const QUrl &source)
{
    if (source == m_source)
        return;
    m_source = source;
    emit sourceChanged();
    start();
}

int WallpaperVideo::fillMode() const
{
    return m_fillMode;
}

void WallpaperVideo::setFillMode(int mode)
{
    if (mode == m_fillMode)
        return;
    m_fillMode = mode;
    emit fillModeChanged();
    update();
}

bool WallpaperVideo::ready() const
{
    return m_ready;
}

void WallpaperVideo::start()
{
    stop();
    if (m_ready) {
        m_ready = false;
        emit readyChanged();
    }
    {
        QMutexLocker lock(&m_mutex);
        m_frame = {};
        m_newFrame = true;
    }
    update();
    if (!m_source.isLocalFile())
        return;
#ifdef Q_OS_WIN
    m_player = new QMediaPlayer(this);
    auto *sink = new QVideoSink(m_player);
    m_player->setVideoSink(sink);
    m_player->setLoops(QMediaPlayer::Infinite);
    connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        QImage image = frame.toImage();
        const int width = frameWidth(m_blur);
        if (image.width() > width)
            image = image.scaledToWidth(width, Qt::FastTransformation);
        if (!image.isNull())
            deliver(std::move(image));
    });
    m_player->setSource(m_source);
    m_player->play();
#else
    const QString path = m_source.toLocalFile();
    m_stop = false;
    m_thread = std::thread([this, path] { decode(path); });
#endif
}

void WallpaperVideo::stop()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
#ifdef Q_OS_WIN
    if (m_player) {
        m_player->stop();
        m_player->deleteLater();
        m_player = nullptr;
    }
#endif
}

bool WallpaperVideo::active() const
{
    return m_active;
}

void WallpaperVideo::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
#ifdef Q_OS_WIN
    if (m_player)
        active ? m_player->play() : m_player->pause();
#endif
    emit activeChanged();
}

qreal WallpaperVideo::blur() const
{
    return m_blur;
}

void WallpaperVideo::setBlur(qreal blur)
{
    if (blur == m_blur)
        return;
    m_blur = blur;
    emit blurChanged();
}

void WallpaperVideo::deliver(QImage frame)
{
    frame = WallpaperBlur::apply(frame, m_blur);
    {
        QMutexLocker lock(&m_mutex);
        m_frame = std::move(frame);
        m_newFrame = true;
    }
    QMetaObject::invokeMethod(this, [this] {
        if (!m_ready) {
            m_ready = true;
            emit readyChanged();
        }
        update();
    }, Qt::QueuedConnection);
}

#ifdef Q_OS_WIN
void WallpaperVideo::decode(const QString &)
{
}
#else
void WallpaperVideo::decode(const QString &path)
{
    // FFmpeg takes UTF-8 paths.
    const QByteArray file = QFile::encodeName(path);
    AVFormatContext *format = nullptr;
    if (avformat_open_input(&format, file.constData(), nullptr, nullptr) < 0) {
        qWarning().noquote() << "WallpaperVideo: cannot open" << path;
        return;
    }
    AVCodecContext *codec = nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    const auto cleanup = qScopeGuard([&] {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    });

    const int stream = avformat_find_stream_info(format, nullptr) < 0
                           ? -1 : av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0) {
        qWarning().noquote() << "WallpaperVideo: no video in" << path;
        return;
    }
    const AVCodecParameters *parameters = format->streams[stream]->codecpar;
    // dav1d decodes AV1 in software; FFmpeg's own "av1" decoder needs GPU support.
    const AVCodec *decoder = parameters->codec_id == AV_CODEC_ID_AV1 ? avcodec_find_decoder_by_name("libdav1d") : nullptr;
    if (!decoder)
        decoder = avcodec_find_decoder(parameters->codec_id);
    codec = decoder ? avcodec_alloc_context3(decoder) : nullptr;
    if (!codec || avcodec_parameters_to_context(codec, parameters) < 0) {
        qWarning().noquote() << "WallpaperVideo: no decoder for" << path;
        return;
    }
    codec->thread_count = 0; // automatic
    if (avcodec_open2(codec, decoder, nullptr) < 0) {
        qWarning().noquote() << "WallpaperVideo: cannot decode" << path;
        return;
    }

    const double timeBase = av_q2d(format->streams[stream]->time_base);
    QElapsedTimer clock;
    clock.start();
    double loopStart = 0; // clock time (s) the current pass started
    bool any = false;     // a frame came out of this pass (else stop instead of looping forever)
    bool failedImage = false;
    while (!m_stop) {
        const int read = av_read_frame(format, packet);
        if (read < 0) { // end: flush, then start over
            avcodec_send_packet(codec, nullptr);
        } else if (packet->stream_index != stream) {
            av_packet_unref(packet);
            continue;
        } else {
            avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
        }
        while (!m_stop && avcodec_receive_frame(codec, frame) == 0) {
            any = true;
            const double at = frame->best_effort_timestamp == AV_NOPTS_VALUE ? 0 : frame->best_effort_timestamp * timeBase;
            QImage image = toImage(frame, frameWidth(m_blur));
            av_frame_unref(frame);
            if (image.isNull()) {
                if (!failedImage)
                    qWarning().noquote() << "WallpaperVideo: unsupported pixel format in" << path;
                failedImage = true;
                continue;
            }
            // Hidden window: hold this frame (and the clock) until shown again.
            if (!m_active) {
                const qint64 pausedAt = clock.elapsed();
                while (!m_stop && !m_active)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                loopStart += (clock.elapsed() - pausedAt) / 1000.0;
            }
            // Wait until the frame is due, in small steps so stop() is not held up.
            while (!m_stop) {
                const double wait = loopStart + at - clock.elapsed() / 1000.0;
                if (wait <= 0)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(static_cast<int>(wait * 1000), 1, 20)));
            }
            deliver(std::move(image));
        }
        if (read < 0) {
            if (!any)
                return;
            any = false;
            av_seek_frame(format, stream, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codec);
            loopStart = clock.elapsed() / 1000.0;
        }
    }
}

#endif

QSGNode *WallpaperVideo::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGImageNode *>(oldNode);
    QImage frame;
    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        if (m_newFrame) {
            frame = m_frame;
            m_newFrame = false;
            changed = true;
        }
    }
    if (changed && frame.isNull()) {
        delete node;
        return nullptr;
    }
    if (!node) {
        if (frame.isNull())
            return nullptr;
        node = window()->createImageNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    if (changed)
        node->setTexture(window()->createTextureFromImage(frame));

    const QSizeF image = node->texture()->textureSize();
    const QRectF bounds = boundingRect();
    QRectF target = bounds;
    QRectF source(QPointF(0, 0), image);
    if (m_fillMode != Stretch && !image.isEmpty() && !bounds.isEmpty()) {
        const qreal scale = m_fillMode == PreserveAspectFit
                                ? std::min(bounds.width() / image.width(), bounds.height() / image.height())
                                : std::max(bounds.width() / image.width(), bounds.height() / image.height());
        const QSizeF shown = image * scale;
        if (m_fillMode == PreserveAspectFit) {
            target = QRectF(bounds.center() - QPointF(shown.width(), shown.height()) / 2, shown);
        } else {
            const QSizeF part(bounds.width() / scale, bounds.height() / scale);
            source = QRectF(QPointF((image.width() - part.width()) / 2, (image.height() - part.height()) / 2), part);
        }
    }
    node->setRect(target);
    node->setSourceRect(source);
    return node;
}

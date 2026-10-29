#include "core/WallpaperBlur.h"

#include <algorithm>
#include <vector>

namespace {

constexpr int kSharpWidth = 1920;
constexpr int kBlurredWidth = 640;
// Matches the old GPU blur: up to 64 px on a 1920 px wide window.
constexpr qreal kMaxRadius = 64.0 * kBlurredWidth / 1920;
constexpr int kDarken = 64; // the old brightness -0.25

// One box blur pass along a line of pixels (stride apart), with a running sum.
void boxLine(QRgb *line, int count, int stride, int radius, std::vector<QRgb> &copy)
{
    copy.resize(count);
    for (int i = 0; i < count; ++i)
        copy[i] = line[i * stride];
    int r = 0, g = 0, b = 0;
    const auto at = [&](int i) { return copy[std::clamp(i, 0, count - 1)]; };
    for (int i = -radius; i <= radius; ++i) {
        r += qRed(at(i));
        g += qGreen(at(i));
        b += qBlue(at(i));
    }
    const int size = 2 * radius + 1;
    for (int i = 0; i < count; ++i) {
        line[i * stride] = qRgb(r / size, g / size, b / size);
        const QRgb out = at(i - radius);
        const QRgb in = at(i + radius + 1);
        r += qRed(in) - qRed(out);
        g += qGreen(in) - qGreen(out);
        b += qBlue(in) - qBlue(out);
    }
}

} // namespace

QImage WallpaperBlur::apply(const QImage &source, qreal blur)
{
    if (source.isNull())
        return {};
    const int maxWidth = blur > 0 ? kBlurredWidth : kSharpWidth;
    QImage image = (source.width() > maxWidth ? source.scaledToWidth(maxWidth, Qt::SmoothTransformation) : source)
                       .convertToFormat(QImage::Format_RGB32);
    const int width = image.width();
    const int height = image.height();
    auto *pixels = reinterpret_cast<QRgb *>(image.bits());
    const int stride = image.bytesPerLine() / 4;
    // Three box passes look like a Gaussian; each covers a third of the radius.
    const int radius = qRound(std::clamp(blur, 0.0, 1.0) * kMaxRadius / 3 * (width / qreal(kBlurredWidth)));
    std::vector<QRgb> copy;
    for (int pass = 0; radius > 0 && pass < 3; ++pass) {
        for (int y = 0; y < height; ++y)
            boxLine(pixels + y * stride, width, 1, radius, copy);
        for (int x = 0; x < width; ++x)
            boxLine(pixels + x, height, stride, radius, copy);
    }
    for (int y = 0; y < height; ++y) {
        QRgb *line = pixels + y * stride;
        for (int x = 0; x < width; ++x)
            line[x] = qRgb(std::max(qRed(line[x]) - kDarken, 0), std::max(qGreen(line[x]) - kDarken, 0),
                           std::max(qBlue(line[x]) - kDarken, 0));
    }
    return image;
}

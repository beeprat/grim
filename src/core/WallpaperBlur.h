#pragma once

#include <QImage>

// The window background's look: scaled down, blurred and darkened once on the CPU, so the window
// only stretches a finished texture (no GPU blur redone on every resize or frame).
namespace WallpaperBlur {

// blur 0..1 (Settings → Appearance → Blur). The result is at most 1920 px wide when sharp, 640 when
// blurred (it is stretched to the window, which the blur hides), and darkened like before.
QImage apply(const QImage &image, qreal blur);

} // namespace WallpaperBlur

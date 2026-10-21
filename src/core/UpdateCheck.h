#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <optional>

// Reads GitHub's "latest release" answer (GET /repos/beeprat/grim/releases/latest) and compares versions.
namespace UpdateCheck {

struct Release
{
    QString version; // tag without the leading "v", e.g. "0.1.2"
    QUrl pageUrl;    // the release's download page
};

// Nothing for anything but a release with a tag and an https page URL.
std::optional<Release> parse(const QByteArray &json);
// True if `latest` is a higher version than `current` ("0.1.10" > "0.1.9").
bool isNewer(const QString &latest, const QString &current);

} // namespace UpdateCheck

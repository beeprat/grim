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
    QUrl installerUrl; // its grim-setup.exe; empty if the release has none
};

// Nothing for anything but a release with a tag and a GitHub page URL. The installer is only taken from
// this repository's release downloads, since it gets run.
std::optional<Release> parse(const QByteArray &json);
// True if `latest` is a higher version than `current` ("0.1.10" > "0.1.9").
bool isNewer(const QString &latest, const QString &current);

} // namespace UpdateCheck

#include "core/UpdateCheck.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVersionNumber>

namespace UpdateCheck {

std::optional<Release> parse(const QByteArray &json)
{
    const QJsonObject release = QJsonDocument::fromJson(json).object();
    QString tag = release.value(QLatin1String("tag_name")).toString();
    if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        tag.remove(0, 1);
    const QUrl page(release.value(QLatin1String("html_url")).toString());
    // The URL is opened in the browser: only GitHub release pages.
    if (tag.isEmpty() || release.value(QLatin1String("draft")).toBool()
        || release.value(QLatin1String("prerelease")).toBool() || page.scheme() != QLatin1String("https")
        || page.host() != QLatin1String("github.com"))
        return std::nullopt;
    QUrl installer;
    for (const QJsonValue &asset : release.value(QLatin1String("assets")).toArray()) {
        const QString url = asset.toObject().value(QLatin1String("browser_download_url")).toString();
        if (asset.toObject().value(QLatin1String("name")).toString() == QLatin1String("grim-setup.exe")
            && url.startsWith(QLatin1String("https://github.com/beeprat/grim/releases/download/")))
            installer = QUrl(url);
    }
    return Release{tag, page, installer};
}

bool isNewer(const QString &latest, const QString &current)
{
    const QVersionNumber latestVersion = QVersionNumber::fromString(latest);
    return !latestVersion.isNull() && latestVersion > QVersionNumber::fromString(current);
}

} // namespace UpdateCheck

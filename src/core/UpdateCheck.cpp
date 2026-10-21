#include "core/UpdateCheck.h"

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
    return Release{tag, page};
}

bool isNewer(const QString &latest, const QString &current)
{
    const QVersionNumber latestVersion = QVersionNumber::fromString(latest);
    return !latestVersion.isNull() && latestVersion > QVersionNumber::fromString(current);
}

} // namespace UpdateCheck

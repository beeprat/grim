#include "controllers/UpdateController.h"

#include "core/UpdateCheck.h"

#include <QJSEngine>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

UpdateController *s_instance = nullptr;

const auto kLatestReleaseUrl = QStringLiteral("https://api.github.com/repos/beeprat/grim/releases/latest");

} // namespace

UpdateController::UpdateController(const QString &currentVersion, QObject *parent)
    : QObject(parent)
    , m_currentVersion(currentVersion)
    , m_network(new QNetworkAccessManager(this))
{
    Q_ASSERT(!s_instance);
    s_instance = this;
}

UpdateController::~UpdateController()
{
    s_instance = nullptr;
}

UpdateController *UpdateController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "UpdateController::create", "construct UpdateController before loading QML");
    // Owned by main.cpp; stop the QML engine from deleting it.
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString UpdateController::latestVersion() const
{
    return m_latestVersion;
}

QUrl UpdateController::downloadUrl() const
{
    return m_downloadUrl;
}

void UpdateController::check()
{
    QNetworkRequest request{QUrl(kLatestReleaseUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GRIM/%1").arg(m_currentVersion));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(15000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const std::optional<UpdateCheck::Release> release = UpdateCheck::parse(reply->readAll());
        if (!release || !UpdateCheck::isNewer(release->version, m_currentVersion))
            return;
        m_latestVersion = release->version;
        m_downloadUrl = release->pageUrl;
        emit updateFound();
    });
}

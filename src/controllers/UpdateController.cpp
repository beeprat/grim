#include "controllers/UpdateController.h"

#include "core/UpdateCheck.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>

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
        m_installerUrl = release->installerUrl;
        emit updateFound();
    });
}

UpdateController::State UpdateController::state() const
{
    return m_state;
}

double UpdateController::progress() const
{
    return m_progress;
}

void UpdateController::setState(State state)
{
    m_state = state;
    emit stateChanged();
}

bool UpdateController::installedByInstaller() const
{
#ifdef Q_OS_WIN
    // installer/grim.nsi installs per user into %LOCALAPPDATA%\Programs\grim.
    const QString installDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                               + QStringLiteral("/Programs/grim");
    return QDir(QCoreApplication::applicationDirPath()) == QDir(installDir);
#else
    return false;
#endif
}

void UpdateController::install()
{
    if (m_state == Downloading || m_state == Installing)
        return;
    if (m_state == Failed || m_installerUrl.isEmpty() || !installedByInstaller()) {
        QDesktopServices::openUrl(m_downloadUrl);
        return;
    }
    m_progress = 0;
    setState(Downloading);
    QNetworkRequest request(m_installerUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GRIM/%1").arg(m_currentVersion));
    request.setTransferTimeout(60000);
    QNetworkReply *reply = m_network->get(request); // follows GitHub's https redirect to its file host
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_progress = total > 0 ? double(received) / double(total) : 0;
        emit stateChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray installer = reply->readAll();
        // A Windows executable, not an error page.
        if (reply->error() != QNetworkReply::NoError || installer.size() < 1024 * 1024 || !installer.startsWith("MZ")) {
            qWarning() << "UpdateController: download failed:" << reply->errorString();
            setState(Failed);
            return;
        }
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                 .filePath(QStringLiteral("grim-setup-%1.exe").arg(m_latestVersion));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(installer) != installer.size()) {
            setState(Failed);
            return;
        }
        file.close();
        // /S: no pages; the installer waits for this process to end, then starts the new version.
        if (!QProcess::startDetached(path, {QStringLiteral("/S")})) {
            setState(Failed);
            return;
        }
        setState(Installing);
        QCoreApplication::quit(); // saves the player state on the way out
    });
}

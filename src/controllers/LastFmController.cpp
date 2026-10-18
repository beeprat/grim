#include "controllers/LastFmController.h"

#include "core/KeyFile.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QSaveFile>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace {

const auto kApiKeyKey = "lastfm/apiKey"_L1;
const auto kSecretKey = "lastfm/apiSecret"_L1;
const auto kSessionKey = "lastfm/sessionKey"_L1;
const auto kUserKey = "lastfm/user"_L1;
// The API key the stored session was made with: a session only works with its own API account.
const auto kSessionApiKey = "lastfm/sessionApiKey"_L1;

LastFmClient::Scrobble scrobbleOf(const Track &track, const QDateTime &startedUtc)
{
    return {track.artist, track.title, track.album, track.durationMs, startedUtc.toSecsSinceEpoch()};
}

} // namespace

LastFmController::LastFmController(const QString &queueFile, const QString &keysFile, QObject *parent)
    : QObject(parent)
    , m_client(new LastFmClient(queueFile, this))
    , m_keysFile(keysFile)
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    QSettings stored;
    // First start with the key file: write it, carrying over the key and secret older versions kept in the registry.
    if (!QFileInfo::exists(m_keysFile)) {
        QSaveFile file(m_keysFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(u"# Last.fm scrobbling for grim. Edit, save, then press \"Connect account\" in Settings.\n"
                       "#\n"
                       "# Create an API account at last.fm/api/account/create (any name, the callback URL\n"
                       "# can stay empty) and copy its API key and shared secret here.\n"
                       "# Keep this file private: the secret belongs to your Last.fm API account.\n"
                       "\n"
                       "api_key = %1\n"
                       "shared_secret = %2\n"_s.arg(stored.value(kApiKeyKey).toString(), stored.value(kSecretKey).toString())
                           .toUtf8());
            if (file.commit()) {
                // The existing session was made with this key.
                if (!stored.contains(kSessionApiKey))
                    stored.setValue(kSessionApiKey, stored.value(kApiKeyKey));
                stored.remove(kApiKeyKey);
                stored.remove(kSecretKey);
            }
        }
    }
    // shortcut: the session key stays in plain QSettings (HKCU registry); move to Windows Credential Manager if shared PCs matter.
    m_userName = stored.value(kUserKey).toString();
    m_client->setSessionKey(stored.value(kSessionKey).toString());
    loadKeys();

    connect(m_client, &LastFmClient::errorOccurred, this, &LastFmController::setErrorText);
    connect(m_client, &LastFmClient::pendingCountChanged, this, &LastFmController::pendingCountChanged);
    connect(m_client, &LastFmClient::sessionRejected, this, [this] {
        setSession({}, {});
        setErrorText(tr("Last.fm no longer accepts this connection. Connect the account again."));
    });
    // Plays queued during an earlier run.
    m_client->flush();
}

LastFmController::~LastFmController()
{
    s_instance = nullptr;
}

LastFmController *LastFmController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "LastFmController::create", "construct LastFmController before loading QML");
    // Owned by main.cpp; stop the QML engine from deleting it.
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString LastFmController::userName() const
{
    return m_userName;
}

bool LastFmController::awaitingApproval() const
{
    return !m_token.isEmpty();
}

QString LastFmController::errorText() const
{
    return m_errorText;
}

int LastFmController::pendingCount() const
{
    return m_client->pendingCount();
}

void LastFmController::loadKeys()
{
    const QHash<QString, QString> keys = KeyFile::read(m_keysFile);
    m_apiKey = keys.value(u"api_key"_s);
    m_apiSecret = keys.value(u"shared_secret"_s);
    m_client->setApiKey(m_apiKey, m_apiSecret);
    // A session belongs to the API account that created it: a different key in the file ends it.
    if (!m_userName.isEmpty() && QSettings().value(kSessionApiKey).toString() != m_apiKey)
        setSession({}, {});
}

void LastFmController::connectAccount()
{
    loadKeys(); // picks up edits to the key file
    if (m_apiKey.isEmpty() || m_apiSecret.isEmpty()) {
        setErrorText(tr("Add your Last.fm API key and shared secret to %1").arg(QDir::toNativeSeparators(m_keysFile)));
        return;
    }
    setErrorText({});
    m_client->requestToken([this](const QString &token) {
        if (token.isEmpty())
            return;
        m_token = token;
        emit awaitingApprovalChanged();
        QDesktopServices::openUrl(LastFmClient::authPage(m_apiKey, token));
    });
}

void LastFmController::finishConnect()
{
    if (m_token.isEmpty())
        return;
    setErrorText({});
    m_client->requestSession(m_token, [this](const QString &user, const QString &sessionKey) {
        if (sessionKey.isEmpty())
            return; // not approved yet: the token stays valid for an hour, so the user can retry
        m_token.clear();
        emit awaitingApprovalChanged();
        setSession(user, sessionKey);
        m_client->flush();
    });
}

void LastFmController::disconnectAccount()
{
    m_token.clear();
    emit awaitingApprovalChanged();
    setSession({}, {});
    setErrorText({});
}

void LastFmController::trackStarted(const Track &track)
{
    if (!m_userName.isEmpty() && !track.artist.isEmpty() && !track.title.isEmpty())
        m_client->updateNowPlaying(scrobbleOf(track, QDateTime::currentDateTimeUtc()));
}

void LastFmController::trackListened(const Track &track, const QDateTime &startedUtc)
{
    // Untagged files would show up on Last.fm under their file name; skip them.
    if (!m_userName.isEmpty() && !track.artist.isEmpty() && !track.title.isEmpty())
        m_client->enqueue(scrobbleOf(track, startedUtc));
}

void LastFmController::setErrorText(const QString &text)
{
    if (text == m_errorText)
        return;
    m_errorText = text;
    emit errorTextChanged();
}

void LastFmController::setSession(const QString &user, const QString &sessionKey)
{
    QSettings stored;
    stored.setValue(kUserKey, user);
    stored.setValue(kSessionKey, sessionKey);
    stored.setValue(kSessionApiKey, sessionKey.isEmpty() ? QString() : m_apiKey);
    m_client->setSessionKey(sessionKey);
    m_userName = user;
    emit settingsChanged();
}

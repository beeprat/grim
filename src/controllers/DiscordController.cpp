#include "controllers/DiscordController.h"
#include "controllers/PlayerController.h"
#include "core/KeyFile.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QJSEngine>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>

#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

const auto kEnabledKey = "discord/enabled"_L1;
const auto kApplicationIdKey = "discord/applicationId"_L1;
const auto kImgurKey = "discord/imgurClientId"_L1;
constexpr int kCoverSize = 512;
// Discord allows about 5 presence updates per 20 s; this collapses bursts (skipping, seeking).
constexpr int kDebounceMs = 1500;
constexpr qint64 kSeekToleranceMs = 3000;

// Discord rejects presence strings shorter than 2 or longer than 128 characters.
QString fit(QString text)
{
    text = text.trimmed();
    if (text.size() > 128)
        text = text.left(127) + u'…';
    while (text.size() < 2)
        text += u'​'; // zero-width space
    return text;
}

} // namespace

DiscordController::DiscordController(PlayerController &player, const QString &coverCacheDir,
                                     const QString &uploadCacheFile, const QString &keysFile, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_keysFile(keysFile)
    , m_ipc(new DiscordIpc(this))
    , m_imgur(new ImgurUploader(uploadCacheFile, this))
    , m_covers(coverCacheDir, 16LL * 1024 * 1024)
    , m_debounce(new QTimer(this))
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    QSettings stored;
    m_enabled = stored.value(kEnabledKey, false).toBool();
    // First start with the key file: write it, carrying over the IDs older versions kept in the registry.
    if (!QFileInfo::exists(m_keysFile)) {
        QSaveFile file(m_keysFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QStringLiteral("# Discord Rich Presence for grim. Edit, save, then turn presence off and on in Settings.\n"
                                      "#\n"
                                      "# application_id: create an application at discord.com/developers/applications\n"
                                      "#   (its name is what Discord shows) and copy its Application ID.\n"
                                      "# imgur_client_id: optional, for album covers. Register an app (anonymous usage)\n"
                                      "#   at api.imgur.com/oauth2/addclient. Covers are uploaded anonymously;\n"
                                      "#   anyone with the link can see them.\n"
                                      "\n"
                                      "application_id = %1\n"
                                      "imgur_client_id = %2\n")
                           .arg(stored.value(kApplicationIdKey).toString(), stored.value(kImgurKey).toString())
                           .toUtf8());
            if (file.commit()) {
                stored.remove(kApplicationIdKey);
                stored.remove(kImgurKey);
            }
        }
    }
    loadKeys();

    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &DiscordController::publish);
    connect(m_ipc, &DiscordIpc::readyChanged, this, &DiscordController::connectedChanged);
    connect(m_imgur, &ImgurUploader::errorOccurred, this, &DiscordController::setErrorText);
    connect(m_ipc, &DiscordIpc::errorOccurred, this, [this](const QString &message) {
        setErrorText(tr("Discord: %1").arg(message));
    });

    connect(&m_player, &PlayerController::trackChanged, this, &DiscordController::onTrackChanged);
    connect(&m_player, &PlayerController::playingChanged, this, &DiscordController::schedule);
    connect(&m_player, &PlayerController::effectsChanged, this, &DiscordController::schedule);
    connect(&m_player, &PlayerController::positionChanged, this, [this] {
        // A seek moves the track's start time; normal playback keeps it.
        const qint64 start = QDateTime::currentMSecsSinceEpoch() - qint64(m_player.positionMs() / m_player.speed());
        if (m_player.playing() && std::llabs(start - m_sentStartMs) > kSeekToleranceMs)
            schedule();
    });

    restart();
}

DiscordController::~DiscordController()
{
    s_instance = nullptr;
}

DiscordController *DiscordController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "DiscordController::create", "construct DiscordController before loading QML");
    // Owned by main.cpp; stop the QML engine from deleting it.
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

bool DiscordController::enabled() const
{
    return m_enabled;
}

void DiscordController::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    QSettings().setValue(kEnabledKey, enabled);
    emit settingsChanged();
    restart();
}

QString DiscordController::keysFile() const
{
    return QDir::toNativeSeparators(m_keysFile);
}

void DiscordController::loadKeys()
{
    const QHash<QString, QString> keys = KeyFile::read(m_keysFile);
    m_applicationId = keys.value(QStringLiteral("application_id"));
    m_imgurClientId = keys.value(QStringLiteral("imgur_client_id"));
    m_imgur->setClientId(m_imgurClientId);
}

bool DiscordController::connected() const
{
    return m_ipc->isReady();
}

QString DiscordController::errorText() const
{
    return m_errorText;
}

void DiscordController::restart()
{
    // Turning presence on picks up edits to the key file.
    if (m_enabled)
        loadKeys();
    if (m_enabled && m_applicationId.isEmpty())
        setErrorText(tr("No Discord application ID in %1").arg(keysFile()));
    else if (m_enabled)
        setErrorText({});
    if (m_enabled && !m_applicationId.isEmpty()) {
        m_ipc->start(m_applicationId);
        onTrackChanged();
    } else {
        m_ipc->stop();
    }
}

void DiscordController::onTrackChanged()
{
    m_coverUrl.clear();
    m_coverKey.clear();
    schedule();
    if (!m_enabled || !m_player.hasTrack())
        return;

    const QImage cover = m_covers.load(m_player.url().toLocalFile(), QSize(kCoverSize, kCoverSize));
    if (cover.isNull())
        return;
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    cover.save(&buffer, "JPG", 90);

    // The same encoder on the same pixels gives the same bytes, so one album's tracks share a key.
    m_coverKey = ImgurUploader::keyOf(jpeg);
    m_coverUrl = m_imgur->cachedUrl(m_coverKey);
    if (!m_coverUrl.isEmpty() || m_imgurClientId.isEmpty())
        return;
    const QString key = m_coverKey;
    m_imgur->upload(jpeg, [this, key](const QString &url) {
        if (url.isEmpty() || key != m_coverKey)
            return; // failed, or the track changed meanwhile
        m_coverUrl = url;
        schedule();
    });
}

void DiscordController::schedule()
{
    // Never restart a running timer: position ticks every 200 ms would postpone it forever.
    // publish() reads the state when it fires, so later changes in the window are included.
    if (m_enabled && !m_debounce->isActive())
        m_debounce->start();
    else
        m_ipc->setActivity({});
}

void DiscordController::publish()
{
    // Paused or stopped: no presence, rather than a frozen progress bar.
    if (!m_enabled || !m_player.hasTrack() || !m_player.playing()) {
        m_sentStartMs = 0;
        m_ipc->setActivity({});
        return;
    }

    // Wall-clock times: a slowed track takes 1/speed as long.
    const double speed = m_player.speed();
    m_sentStartMs = QDateTime::currentMSecsSinceEpoch() - qint64(m_player.positionMs() / speed);
    QJsonObject timestamps{{"start"_L1, m_sentStartMs}};
    if (m_player.durationMs() > 0)
        timestamps.insert("end"_L1, m_sentStartMs + qint64(m_player.durationMs() / speed));

    QJsonObject activity{
        {"type"_L1, 2},                // Listening
        {"status_display_type"_L1, 1}, // member list shows "Listening to <state>", i.e. the artist
        {"details"_L1, fit(m_player.title())},
        {"state"_L1, fit(m_player.artist().isEmpty() ? tr("Unknown artist") : m_player.artist())},
        {"timestamps"_L1, timestamps},
    };
    QJsonObject assets;
    if (!m_coverUrl.isEmpty())
        assets.insert("large_image"_L1, m_coverUrl);
    if (!m_player.album().isEmpty())
        assets.insert("large_text"_L1, fit(tr("on %1").arg(m_player.album())));
    if (!assets.isEmpty())
        activity.insert("assets"_L1, assets);
    m_ipc->setActivity(activity);
}

void DiscordController::setErrorText(const QString &text)
{
    if (text == m_errorText)
        return;
    m_errorText = text;
    emit errorTextChanged();
}

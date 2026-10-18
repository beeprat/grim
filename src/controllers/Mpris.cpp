#include "controllers/Mpris.h"

#include "controllers/PlayerController.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QWindow>

using namespace Qt::StringLiterals;

namespace {

const QString kObjectPath = u"/org/mpris/MediaPlayer2"_s;
const QString kService = u"org.mpris.MediaPlayer2.grim"_s;

// org.mpris.MediaPlayer2: the application itself.
class RootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit RootAdaptor(Mpris *owner)
        : QDBusAbstractAdaptor(owner)
        , m_owner(owner)
    {
    }

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return u"grim."_s; }
    QString desktopEntry() const { return u"grim"_s; }
    QStringList supportedUriSchemes() const { return {u"file"_s}; }
    QStringList supportedMimeTypes() const
    {
        return {u"audio/flac"_s, u"audio/mpeg"_s, u"audio/wav"_s, u"audio/mp4"_s, u"audio/aac"_s};
    }

public slots:
    void Raise()
    {
        if (QWindow *window = m_owner->window()) {
            window->show();
            window->raise();
            window->requestActivate();
        }
    }
    void Quit() { QCoreApplication::quit(); }

private:
    Mpris *m_owner;
};

// org.mpris.MediaPlayer2.Player: playback state and controls.
class PlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ hasTrack)
    Q_PROPERTY(bool CanGoPrevious READ hasTrack)
    Q_PROPERTY(bool CanPlay READ hasTrack)
    Q_PROPERTY(bool CanPause READ hasTrack)
    Q_PROPERTY(bool CanSeek READ hasTrack)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit PlayerAdaptor(Mpris *owner)
        : QDBusAbstractAdaptor(owner)
        , m_player(owner->player())
    {
        connect(&m_player, &PlayerController::playingChanged, this,
                [this] { notify({{u"PlaybackStatus"_s, playbackStatus()}}); });
        connect(&m_player, &PlayerController::trackChanged, this, [this] {
            notify({{u"Metadata"_s, metadata()},
                    {u"CanGoNext"_s, hasTrack()},
                    {u"CanGoPrevious"_s, hasTrack()},
                    {u"CanPlay"_s, hasTrack()},
                    {u"CanPause"_s, hasTrack()},
                    {u"CanSeek"_s, hasTrack()}});
        });
        connect(&m_player, &PlayerController::volumeChanged, this, [this] { notify({{u"Volume"_s, volume()}}); });
    }

    QString playbackStatus() const
    {
        return m_player.playing() ? u"Playing"_s : m_player.hasTrack() ? u"Paused"_s : u"Stopped"_s;
    }
    QVariantMap metadata() const
    {
        if (!m_player.hasTrack())
            return {};
        QVariantMap map{
            {u"mpris:trackid"_s, QVariant::fromValue(QDBusObjectPath(u"/org/grim/track/current"_s))},
            {u"mpris:length"_s, qlonglong(m_player.durationMs() * 1000)},
            {u"xesam:title"_s, m_player.title()},
            {u"xesam:album"_s, m_player.album()},
            {u"xesam:url"_s, m_player.url().toString()},
        };
        if (!m_player.artist().isEmpty())
            map.insert(u"xesam:artist"_s, QStringList{m_player.artist()});
        return map;
    }
    qlonglong position() const { return m_player.positionMs() * 1000; }
    double volume() const { return m_player.volume(); }
    void setVolume(double volume) { m_player.setVolume(qBound(0.0, volume, 1.0)); }
    double rate() const { return 1.0; }
    bool hasTrack() const { return m_player.hasTrack(); }
    bool canControl() const { return true; }

public slots:
    void Play()
    {
        if (!m_player.playing())
            m_player.togglePlay();
    }
    void Pause()
    {
        if (m_player.playing())
            m_player.togglePlay();
    }
    void PlayPause() { m_player.togglePlay(); }
    // Like the Windows Stop media key: pauses, keeping the position.
    void Stop() { Pause(); }
    void Next() { m_player.next(); }
    void Previous() { m_player.previous(); }
    void Seek(qlonglong offsetUs) { seekTo(m_player.positionMs() + offsetUs / 1000); }
    void SetPosition(const QDBusObjectPath &, qlonglong positionUs) { seekTo(positionUs / 1000); }
    void OpenUri(const QString &uri) { m_player.open(QUrl(uri)); }

signals:
    void Seeked(qlonglong positionUs);

private:
    void seekTo(qint64 ms)
    {
        if (!m_player.hasTrack())
            return;
        m_player.seek(qBound<qint64>(0, ms, m_player.durationMs()));
        emit Seeked(position());
    }

    // MPRIS clients learn about changes only from org.freedesktop.DBus.Properties.PropertiesChanged.
    static void notify(const QVariantMap &changed)
    {
        QDBusMessage signal = QDBusMessage::createSignal(kObjectPath, u"org.freedesktop.DBus.Properties"_s,
                                                         u"PropertiesChanged"_s);
        signal << u"org.mpris.MediaPlayer2.Player"_s << changed << QStringList();
        QDBusConnection::sessionBus().send(signal);
    }

    PlayerController &m_player;
};

} // namespace

Mpris::Mpris(PlayerController &player, QObject *parent)
    : QObject(parent)
    , m_player(player)
{
    new RootAdaptor(this);
    new PlayerAdaptor(this);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(kObjectPath, this) || !bus.registerService(kService))
        qWarning("Mpris: cannot publish on the session bus: %s", qPrintable(bus.lastError().message()));
}

Mpris::~Mpris()
{
    QDBusConnection::sessionBus().unregisterService(kService);
}

void Mpris::setWindow(QWindow *window)
{
    m_window = window;
}

#include "Mpris.moc"

#include "controllers/PlayerController.h"

#include "core/LibraryDatabase.h"
#include "core/LyricsFetcher.h"
#include "core/ReplayGain.h"
#include "core/TagReader.h"
#include "providers/CoverImageProvider.h"

#include <QJSEngine>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QtLogging>

#include <algorithm>

namespace {

// Drives the position display and end-of-track detection.
constexpr int kPollIntervalMs = 200;
// While an A-B loop is set: how late the jump back to its start can come.
constexpr int kLoopPollIntervalMs = 15;
// How often a running session's listening time is written, so stats are live
// and a crash loses at most this much.
constexpr int kSaveIntervalMs = 5000;
// Shorter sessions are accidental (double click, skipping through) and not stored.
constexpr qint64 kMinSessionMs = 1000;
// previous() restarts the current track after this point instead of going back.
constexpr qint64 kRestartThresholdMs = 3000;
// Gapless: the next track is opened this long before the current one ends.
constexpr qint64 kQueueAheadMs = 5000;

const auto kLastQueueKey = QStringLiteral("player/lastQueue");
const auto kLastIndexKey = QStringLiteral("player/lastIndex");
const auto kLastPositionKey = QStringLiteral("player/lastPositionMs");
const auto kVolumeKey = QStringLiteral("player/volume");
const auto kShuffleKey = QStringLiteral("player/shuffle");
const auto kRepeatKey = QStringLiteral("player/repeat");

} // namespace

PlayerController::PlayerController(StatsDatabase &stats, QObject *parent)
    : QObject(parent)
    , m_stats(stats)
    , m_pollTimer(new QTimer(this))
    , m_saveTimer(new QTimer(this))
    , m_lyricsFetcher(new LyricsFetcher(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                            + QStringLiteral("/lyrics"),
                                        this))
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &PlayerController::poll);
    m_saveTimer->setInterval(kSaveIntervalMs);
    connect(m_saveTimer, &QTimer::timeout, this, &PlayerController::saveSession);

    // The controls as they were when the app last quit.
    const QSettings settings;
    m_volume = std::clamp(settings.value(kVolumeKey, 1.0).toDouble(), 0.0, 1.0);
    m_engine.setVolume(static_cast<float>(m_volume));
    m_shuffle = settings.value(kShuffleKey, false).toBool();
    m_repeat = static_cast<Repeat>(std::clamp(settings.value(kRepeatKey, int(RepeatOff)).toInt(), int(RepeatOff),
                                              int(RepeatOne)));
}

PlayerController::~PlayerController()
{
    saveLastTrack();
    // Quitting mid-track still counts the time listened so far.
    finishSession();
    s_instance = nullptr;
}

PlayerController *PlayerController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "PlayerController::create", "construct PlayerController before loading QML");
    // Owned by main.cpp; stop the QML engine from deleting it.
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

bool PlayerController::hasTrack() const
{
    return m_engine.hasTrack();
}

QString PlayerController::title() const
{
    return m_track.title;
}

QString PlayerController::artist() const
{
    return m_track.artist;
}

QString PlayerController::album() const
{
    return m_track.album;
}

QString PlayerController::coverUrl() const
{
    return m_track.path.isEmpty() ? QString() : CoverImageProvider::urlFor(m_track.path);
}

QUrl PlayerController::url() const
{
    return m_track.path.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_track.path);
}

qint64 PlayerController::durationMs() const
{
    return m_track.durationMs;
}

bool PlayerController::playing() const
{
    return m_playing;
}

qint64 PlayerController::positionMs() const
{
    return m_positionMs;
}

double PlayerController::volume() const
{
    return m_volume;
}

void PlayerController::setVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(volume, m_volume))
        return;
    m_volume = volume;
    m_engine.setVolume(static_cast<float>(volume));
    emit volumeChanged();
}

QVariantList PlayerController::lyrics() const
{
    QVariantList lines;
    for (const Lyrics::Line &line : m_lyrics)
        lines.append(QVariantMap{{QStringLiteral("time"), line.timeMs}, {QStringLiteral("text"), line.text}});
    return lines;
}

bool PlayerController::lyricsLoading() const
{
    return m_lyricsLoading;
}

void PlayerController::setOnlineLyrics(bool enabled)
{
    m_onlineLyrics = enabled;
}

void PlayerController::setScrobbleAfterMs(qint64 ms)
{
    m_scrobbleAfterMs = ms;
}

void PlayerController::setCrossfadeMs(int ms)
{
    m_crossfadeMs = std::max(ms, 0);
    if (m_crossfadeMs > 0) // crossfade replaces gapless: drop a track queued for a hard cut
        m_engine.cancelQueued();
}

void PlayerController::setSongGains(const LibraryDatabase *songGains)
{
    m_songGains = songGains;
}

float PlayerController::replayGainFactor(const QString &path) const
{
    const std::optional<double> db = m_songGains ? m_songGains->replayGainDb(path) : std::nullopt;
    return db ? ReplayGain::fromDb(*db) : 1.0f;
}

void PlayerController::refreshReplayGain()
{
    m_engine.setTrackGain(replayGainFactor(m_track.path));
}

void PlayerController::loadLyrics()
{
    m_lyricsLoading = false;
    m_lyrics = Lyrics::parse(Lyrics::read(m_track.path));
    if (m_lyrics.isEmpty() && m_onlineLyrics && !m_track.artist.isEmpty() && !m_track.title.isEmpty()) {
        bool known = false;
        const QString cached = m_lyricsFetcher->cached(m_track, &known);
        if (known) {
            m_lyrics = Lyrics::parse(cached);
        } else {
            m_lyricsLoading = true;
            const QString path = m_track.path;
            m_lyricsFetcher->fetch(m_track, [this, path](const QString &text) {
                if (path != m_track.path)
                    return; // the answer is for a track that is no longer playing
                m_lyricsLoading = false;
                m_lyrics = Lyrics::parse(text);
                m_lyricIndex = -2; // force a lyricIndexChanged for the new lines
                updateLyricIndex();
                emit lyricsChanged();
            });
        }
    }
    updateLyricIndex();
    emit lyricsChanged();
}

bool PlayerController::lyricsSynced() const
{
    return !m_lyrics.isEmpty() && m_lyrics.first().timeMs >= 0;
}

int PlayerController::lyricIndex() const
{
    return m_lyricIndex;
}

double PlayerController::speed() const
{
    return m_speed;
}

void PlayerController::setSpeed(double speed)
{
    if (qFuzzyCompare(speed, m_speed))
        return;
    m_speed = speed;
    m_engine.setSpeed(float(speed));
    m_queueTried = false; // a queued next track was dropped: timed for the old speed
    emit effectsChanged();
}

double PlayerController::reverb() const
{
    return m_reverb;
}

void PlayerController::setReverb(double amount)
{
    if (qFuzzyCompare(1 + amount, 1 + m_reverb))
        return;
    m_reverb = amount;
    m_engine.setReverb(float(amount));
    emit effectsChanged();
}

double PlayerController::pitchSemitones() const
{
    return m_pitchSemitones;
}

void PlayerController::setPitchSemitones(double semitones)
{
    if (qFuzzyCompare(1 + semitones, 1 + m_pitchSemitones))
        return;
    m_pitchSemitones = semitones;
    m_engine.setPitchSemitones(float(semitones));
    emit effectsChanged();
}

bool PlayerController::lyricsShown() const
{
    return m_lyricsShown;
}

void PlayerController::setLyricsShown(bool shown)
{
    if (shown == m_lyricsShown)
        return;
    m_lyricsShown = shown;
    emit lyricsShownChanged();
    updateLyricIndex(); // jump to the current line when shown again
}

void PlayerController::updateLyricIndex()
{
    if (!m_lyricsShown)
        return; // nobody is looking: no per-poll lookups or redraws
    const int index = lyricsSynced() ? Lyrics::lineAt(m_lyrics, m_positionMs) : -1;
    if (index == m_lyricIndex)
        return;
    m_lyricIndex = index;
    emit lyricIndexChanged();
}

bool PlayerController::shuffle() const
{
    return m_shuffle;
}

void PlayerController::setShuffle(bool shuffle)
{
    if (shuffle == m_shuffle)
        return;
    m_shuffle = shuffle;
    rebuildOrder();
    emit shuffleChanged();
}

PlayerController::Repeat PlayerController::repeat() const
{
    return m_repeat;
}

void PlayerController::setRepeat(Repeat repeat)
{
    if (repeat == m_repeat)
        return;
    m_repeat = repeat;
    // Repeat one replays this track instead of the queued next one.
    m_engine.cancelQueued();
    m_queueTried = false;
    emit repeatChanged();
}

void PlayerController::playQueue(const QList<QUrl> &fileUrls, int startIndex)
{
    m_queue.clear();
    for (const QUrl &url : fileUrls)
        m_queue.append(url.toLocalFile());
    if (m_queue.isEmpty())
        return;

    // Order is built around the start track, so shuffle still begins with what was clicked.
    m_orderPosition = -1;
    m_order.clear();
    const int start = std::clamp(startIndex, 0, static_cast<int>(m_queue.size()) - 1);
    m_order.append(start);
    rebuildOrder();
    playAt(m_orderPosition);
}

void PlayerController::playTrack(const QList<QUrl> &fileUrls, int index)
{
    if (index < 0 || index >= fileUrls.size())
        return;
    const QString path = fileUrls.at(index).toLocalFile();
    if (!m_engine.hasTrack() || path.compare(m_track.path, Qt::CaseInsensitive) != 0) {
        playQueue(fileUrls, index);
        return;
    }
    // Same track: adopt the list as the queue around it, without reloading, then toggle.
    m_queue.clear();
    for (const QUrl &url : fileUrls)
        m_queue.append(url.toLocalFile());
    m_orderPosition = -1;
    m_order = {index};
    rebuildOrder();
    togglePlay();
}

void PlayerController::open(const QUrl &fileUrl)
{
    playQueue({fileUrl}, 0);
}

void PlayerController::saveLastTrack() const
{
    QSettings settings;
    settings.setValue(kVolumeKey, m_volume);
    settings.setValue(kShuffleKey, m_shuffle);
    settings.setValue(kRepeatKey, int(m_repeat));
    if (!m_engine.hasTrack())
        return; // nothing loaded: keep the track an earlier run saved
    settings.setValue(kLastQueueKey, QStringList(m_queue));
    settings.setValue(kLastIndexKey, m_order.value(m_orderPosition, -1));
    settings.setValue(kLastPositionKey, m_positionMs);
}

void PlayerController::restoreLastTrack()
{
    const QSettings settings;
    const QStringList queue = settings.value(kLastQueueKey).toStringList();
    const int index = settings.value(kLastIndexKey, -1).toInt();
    if (index < 0 || index >= queue.size() || !QFileInfo::exists(queue.at(index)))
        return; // deleted or moved since; start empty

    m_queue = queue;
    m_orderPosition = -1;
    m_order = {index};
    rebuildOrder();
    playAt(m_orderPosition, 0, false);
    if (m_engine.hasTrack())
        seek(settings.value(kLastPositionKey, 0).toLongLong());
}

void PlayerController::releaseFiles(const QStringList &paths)
{
    m_engine.cancelQueued();
    m_queueTried = false;
    if (m_engine.hasTrack() && paths.contains(m_track.path, Qt::CaseInsensitive)) {
        finishSession();
        setPlaying(false);
        m_engine.unload();
        m_track = {};
        m_positionMs = 0;
        emit trackChanged();
        emit positionChanged();
        loadLyrics();
    }
    const QString current = m_queue.value(m_order.value(m_orderPosition, -1));
    m_queue.removeIf([&](const QString &path) { return paths.contains(path, Qt::CaseInsensitive); });
    m_order = {std::max<int>(0, int(m_queue.indexOf(current)))};
    m_orderPosition = -1;
    rebuildOrder();
}

void PlayerController::rebuildOrder()
{
    // The queued next track may no longer be next.
    m_engine.cancelQueued();
    m_queueTried = false;
    const int current = m_orderPosition >= 0 && m_orderPosition < m_order.size() ? m_order.at(m_orderPosition)
                        : !m_order.isEmpty()                                     ? m_order.first()
                                                                                 : 0;
    m_order.clear();
    for (int i = 0; i < m_queue.size(); ++i)
        m_order.append(i);
    if (m_queue.isEmpty()) {
        m_orderPosition = -1;
        return;
    }

    if (m_shuffle) {
        m_order.removeOne(current);
        std::shuffle(m_order.begin(), m_order.end(), *QRandomGenerator::global());
        m_order.prepend(current);
        m_orderPosition = 0;
    } else {
        m_orderPosition = current;
    }
}

void PlayerController::playAt(int orderPosition, int crossfadeMs, bool autoplay)
{
    finishSession();
    setPlaying(false);
    m_orderPosition = orderPosition;

    const QString path = m_queue.value(m_order.value(orderPosition, -1));
    if (path.isEmpty() || !m_engine.load(QFileInfo(path).filesystemFilePath(), crossfadeMs)) {
        qWarning() << "PlayerController: cannot play" << path;
        m_track = {};
        m_positionMs = 0;
        emit trackChanged();
        emit positionChanged();
        loadLyrics();
        return;
    }

    trackLoaded(path);
    if (autoplay)
        play();
}

void PlayerController::trackLoaded(const QString &path)
{
    m_track = TagReader::read(path);
    refreshReplayGain(); // a gapless track got the same gain when it was queued
    // The decoder's frame count is exact; TagLib's is an estimate for VBR MP3.
    if (const qint64 decodedMs = m_engine.durationMs(); decodedMs > 0)
        m_track.durationMs = decodedMs;
    m_positionMs = 0;
    m_queueTried = false;
    applyLoop(-1, -1);
    emit trackChanged();
    emit positionChanged();
    loadLyrics();
}

void PlayerController::queueNextIfDue(qint64 positionMs)
{
    // Only plain advances: crossfade has its own transition, repeat one replays, and the
    // wrap-around of repeat all may reshuffle the order first.
    if (m_queueTried || loopActive() || m_crossfadeMs > 0 || m_repeat == RepeatOne || m_orderPosition + 1 >= m_order.size()
        || m_track.durationMs <= 0 || positionMs < m_track.durationMs - kQueueAheadMs)
        return;
    m_queueTried = true;
    const QString next = m_queue.value(m_order.at(m_orderPosition + 1));
    m_engine.queueNext(QFileInfo(next).filesystemFilePath(), replayGainFactor(next));
}

void PlayerController::togglePlay()
{
    if (!m_engine.hasTrack())
        return;
    if (m_playing)
        pause();
    else
        play();
}

void PlayerController::play()
{
    m_engine.play();
    const bool newSession = !m_sessionStart.isValid();
    if (newSession)
        m_sessionStart = QDateTime::currentDateTimeUtc();
    m_playClock.start();
    setPlaying(true);
    if (newSession)
        emit sessionStarted(m_track);
}

void PlayerController::pause()
{
    m_engine.pause(); // also drops a queued next track
    m_queueTried = false; // queued again after resuming
    stopClock();
    saveSession();
    setPlaying(false);
}

void PlayerController::next()
{
    advance(0);
}

bool PlayerController::advance(int crossfadeMs)
{
    if (m_order.isEmpty())
        return false;
    if (m_orderPosition + 1 < m_order.size()) {
        playAt(m_orderPosition + 1, crossfadeMs);
    } else if (m_repeat != RepeatOff) {
        if (m_shuffle) { // a fresh shuffle for the next round
            std::shuffle(m_order.begin(), m_order.end(), *QRandomGenerator::global());
        }
        playAt(0, crossfadeMs);
    } else {
        return false;
    }
    return true;
}

void PlayerController::previous()
{
    if (m_order.isEmpty())
        return;
    if (m_positionMs > kRestartThresholdMs || (m_orderPosition == 0 && m_repeat == RepeatOff)) {
        seek(0);
        return;
    }
    playAt(m_orderPosition > 0 ? m_orderPosition - 1 : static_cast<int>(m_order.size()) - 1);
}

qint64 PlayerController::loopStartMs() const
{
    return m_loopStartMs;
}

qint64 PlayerController::loopEndMs() const
{
    return m_loopEndMs;
}

void PlayerController::setLoopStart(qint64 positionMs)
{
    if (!m_engine.hasTrack())
        return;
    positionMs = std::clamp<qint64>(positionMs, 0, std::max<qint64>(m_track.durationMs, 0));
    applyLoop(positionMs, m_loopEndMs > positionMs ? m_loopEndMs : -1);
}

void PlayerController::setLoopEnd(qint64 positionMs)
{
    if (!m_engine.hasTrack() || m_loopStartMs < 0)
        return;
    positionMs = std::clamp<qint64>(positionMs, 0, std::max<qint64>(m_track.durationMs, 0));
    if (positionMs == m_loopStartMs)
        return;
    applyLoop(std::min(m_loopStartMs, positionMs), std::max(m_loopStartMs, positionMs));
    if (m_positionMs < m_loopStartMs || m_positionMs >= m_loopEndMs)
        seek(m_loopStartMs); // start hearing the loop right away
}

void PlayerController::clearLoop()
{
    applyLoop(-1, -1);
}

void PlayerController::applyLoop(qint64 startMs, qint64 endMs)
{
    if (startMs == m_loopStartMs && endMs == m_loopEndMs)
        return;
    m_loopStartMs = startMs;
    m_loopEndMs = endMs;
    if (loopActive()) {
        m_engine.cancelQueued(); // a gapless next track would start at the track's end
        m_queueTried = false;
    }
    m_pollTimer->setInterval(loopActive() ? kLoopPollIntervalMs : kPollIntervalMs);
    emit loopChanged();
}

void PlayerController::seek(qint64 positionMs)
{
    positionMs = std::clamp<qint64>(positionMs, 0, std::max<qint64>(m_track.durationMs, 0));
    m_engine.seek(positionMs); // also drops a queued next track, timed for the old position
    m_queueTried = false;
    // Show the target at once; the engine applies the seek on its next mix.
    m_positionMs = positionMs;
    emit positionChanged();
    updateLyricIndex();
}

void PlayerController::poll()
{
    // A-B loop: back to its start at its end (or if the track ran out because the end is the track's end).
    if (loopActive() && (m_engine.atEnd() || m_engine.positionMs() >= m_loopEndMs)) {
        const bool stopped = m_engine.atEnd();
        seek(m_loopStartMs);
        if (stopped)
            m_engine.play();
        return;
    }
    // Gapless: the queued next track already started on the last frame of this one.
    if (m_engine.atEnd() && m_engine.promoteQueued()) {
        m_positionMs = m_track.durationMs;
        finishSession();
        ++m_orderPosition;
        trackLoaded(m_queue.value(m_order.at(m_orderPosition)));
        play(); // already sounding; this starts the new listening session
        return;
    }
    if (m_engine.atEnd()) {
        m_positionMs = m_track.durationMs;
        emit positionChanged();
        finishSession();
        setPlaying(false);

        if (m_repeat == RepeatOne)
            playAt(m_orderPosition);
        else if (m_orderPosition + 1 < m_order.size() || m_repeat == RepeatAll)
            next();
        return;
    }

    m_engine.update();
    if (!m_listenReported && m_sessionStart.isValid()
        && m_listenedMs + (m_playClock.isValid() ? m_playClock.elapsed() : 0) >= m_scrobbleAfterMs) {
        m_listenReported = true;
        emit listenedLongEnough(m_track, m_sessionStart);
    }
    const qint64 position = m_engine.positionMs();
    // Crossfade: start the next track while this one still has crossfadeMs left. Only for
    // automatic changes (not repeat one) and tracks long enough to hold two fades.
    const qint64 duration = m_track.durationMs;
    if (m_crossfadeMs > 0 && !loopActive() && m_repeat != RepeatOne && duration > 2 * m_crossfadeMs
        && position >= duration - m_crossfadeMs && advance(m_crossfadeMs))
        return;
    queueNextIfDue(position);

    if (position != m_positionMs) {
        // QML redraws the seek bar on every signal; once a second matches its m:ss display.
        // Lyrics still follow the exact position.
        const bool newSecond = position / 1000 != m_positionMs / 1000;
        m_positionMs = position;
        if (newSecond)
            emit positionChanged();
        updateLyricIndex();
    }
}

void PlayerController::setPlaying(bool playing)
{
    if (playing) {
        m_pollTimer->start();
        m_saveTimer->start();
    } else {
        m_pollTimer->stop();
        m_saveTimer->stop();
    }

    if (m_playing == playing)
        return;
    m_playing = playing;
    emit playingChanged();
}

void PlayerController::stopClock()
{
    if (!m_playClock.isValid())
        return;
    m_listenedMs += m_playClock.elapsed();
    m_playClock.invalidate();
}

void PlayerController::saveSession()
{
    const qint64 listened = m_listenedMs + (m_playClock.isValid() ? m_playClock.elapsed() : 0);
    if (!m_stats.isOpen() || !m_sessionStart.isValid() || listened < kMinSessionMs)
        return;
    m_stats.recordPlay(m_track, m_sessionStart, listened, &m_playId);
}

void PlayerController::finishSession()
{
    stopClock();
    saveSession();
    m_sessionStart = {};
    m_listenedMs = 0;
    m_playId = 0;
    m_listenReported = false;
}

#pragma once

#include "audio/AudioEngine.h"
#include "core/Lyrics.h"
#include "core/ReplayGain.h"
#include "core/StatsDatabase.h"
#include "models/Track.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class LibraryDatabase;
class LyricsFetcher;
class QJSEngine;
class QQmlEngine;
class QTimer;

// Playback state for QML: the play queue (next/previous, shuffle, repeat) and
// the bridge from playback to listening stats.
// One instance, created in main.cpp before the QML engine (see StatsController).
class PlayerController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString album READ album NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY trackChanged FINAL)
    Q_PROPERTY(QUrl url READ url NOTIFY trackChanged FINAL)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY trackChanged FINAL)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged FINAL)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged FINAL)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)
    // [{time, text}] from the file's LYRICS tag or a sidecar .lrc; time is -1 when unsynced.
    Q_PROPERTY(QVariantList lyrics READ lyrics NOTIFY lyricsChanged FINAL)
    Q_PROPERTY(bool lyricsSynced READ lyricsSynced NOTIFY lyricsChanged FINAL)
    // True while lyrics are being looked up online for the current track.
    Q_PROPERTY(bool lyricsLoading READ lyricsLoading NOTIFY lyricsChanged FINAL)
    // Line being sung now, -1 before the first one or when unsynced.
    Q_PROPERTY(int lyricIndex READ lyricIndex NOTIFY lyricIndexChanged FINAL)
    // False while the lyrics card is hidden: lyricIndex then stops following playback
    // (and catches up when shown again).
    Q_PROPERTY(bool lyricsShown READ lyricsShown WRITE setLyricsShown NOTIFY lyricsShownChanged FINAL)
    // Effects (Settings → Effects), as heard: speed 1 = normal (0.8 = slowed: slower and lower),
    // reverb 0 = none .. 1, pitch in semitones with the tempo kept.
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY effectsChanged FINAL)
    Q_PROPERTY(double reverb READ reverb WRITE setReverb NOTIFY effectsChanged FINAL)
    Q_PROPERTY(double pitchSemitones READ pitchSemitones WRITE setPitchSemitones NOTIFY effectsChanged FINAL)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged FINAL)
    Q_PROPERTY(Repeat repeat READ repeat WRITE setRepeat NOTIFY repeatChanged FINAL)
    // A-B loop inside the current track (-1 = not set); playback jumps back to the start at the end.
    Q_PROPERTY(qint64 loopStartMs READ loopStartMs NOTIFY loopChanged FINAL)
    Q_PROPERTY(qint64 loopEndMs READ loopEndMs NOTIFY loopChanged FINAL)

public:
    enum Repeat {
        RepeatOff,
        RepeatAll, // wrap around at the end of the queue
        RepeatOne, // play the current track again
    };
    Q_ENUM(Repeat)

    explicit PlayerController(StatsDatabase &stats, QObject *parent = nullptr);
    ~PlayerController() override;

    static PlayerController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    bool hasTrack() const;
    QString title() const;
    QString artist() const;
    QString album() const;
    QString coverUrl() const;
    QUrl url() const;
    qint64 durationMs() const;
    bool playing() const;
    qint64 positionMs() const;
    double volume() const;
    void setVolume(double volume);
    QVariantList lyrics() const;
    bool lyricsSynced() const;
    bool lyricsLoading() const;
    // Remembers volume, shuffle, repeat, and the queue, track and position for restoreLastTrack().
    // Runs on quit, and from main.cpp when Windows shuts down or logs off.
    void saveLastTrack() const;
    // Look up lyrics on lrclib.net when the file has none (Settings → Lyrics).
    void setOnlineLyrics(bool enabled);
    // Overlap at automatic track changes, in ms; 0 = off (Settings → Playback).
    void setCrossfadeMs(int ms);
    // ReplayGain::Off/Track/Album (Settings → Playback); a song's own dB value in `songGains` wins.
    void setReplayGainMode(int mode);
    void setSongGains(const LibraryDatabase *songGains);
    // Applies the current settings to the playing track again (a song's dB value changed).
    void refreshReplayGain();
    void setScrobbleAfterMs(qint64 ms);
    int lyricIndex() const;
    bool lyricsShown() const;
    void setLyricsShown(bool shown);
    double speed() const;
    void setSpeed(double speed);
    double reverb() const;
    void setReverb(double amount);
    double pitchSemitones() const;
    void setPitchSemitones(double semitones);
    bool shuffle() const;
    void setShuffle(bool shuffle);
    Repeat repeat() const;
    void setRepeat(Repeat repeat);

    // Replaces the queue with these local files and plays the one at startIndex.
    Q_INVOKABLE void playQueue(const QList<QUrl> &fileUrls, int startIndex);
    // A click on a track row: plays it with this queue, or, if it is the loaded track,
    // pauses/resumes it instead of starting it over (the queue still becomes this list).
    Q_INVOKABLE void playTrack(const QList<QUrl> &fileUrls, int index);
    // Plays a single file (queue of one).
    Q_INVOKABLE void open(const QUrl &fileUrl);
    // Loads, paused, the queue and track (at its position) that were playing when the app last quit.
    void restoreLastTrack();
    // These files are about to be deleted: stops playing them (current or queued for gapless),
    // so they are not held open, and takes them out of the queue.
    void releaseFiles(const QStringList &paths);
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void next();
    // Restarts the track if more than 3 s in; otherwise goes to the previous one.
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 positionMs);

    qint64 loopStartMs() const;
    qint64 loopEndMs() const;
    // Sets the loop start (clearing an end before it) or end (swapped with the start if earlier).
    // A new track clears the loop.
    Q_INVOKABLE void setLoopStart(qint64 positionMs);
    Q_INVOKABLE void setLoopEnd(qint64 positionMs);
    Q_INVOKABLE void clearLoop();

signals:
    void trackChanged();
    void playingChanged();
    void positionChanged();
    void volumeChanged();
    void shuffleChanged();
    void repeatChanged();
    void loopChanged();
    void lyricIndexChanged();
    void lyricsShownChanged();
    void effectsChanged();
    void lyricsChanged();
    // A new listening session began (Last.fm "now playing").
    void sessionStarted(const Track &track);
    // Once per session, when listening time passes scrobbleAfterMs (Last.fm scrobble).
    void listenedLongEnough(const Track &track, const QDateTime &startedUtc);

private:
    void poll();
    // Loads m_queue[m_order[orderPosition]] and starts playing it (or only loads it, paused).
    void playAt(int orderPosition, int crossfadeMs = 0, bool autoplay = true);
    // Reads the newly loaded track's tags and lyrics and announces it.
    void trackLoaded(const QString &path);
    // Gapless: near the end of a track, has the engine queue the next one.
    void queueNextIfDue(qint64 positionMs);
    // next() with an optional crossfade; false at the end of the queue without repeat.
    bool advance(int crossfadeMs);
    // Rebuilds the play order; with shuffle the current track stays first.
    void rebuildOrder();
    void play();
    void pause();
    void setPlaying(bool playing);
    void updateLyricIndex();
    // File tag / sidecar .lrc first, then the online cache, then an online lookup.
    void loadLyrics();
    // Adds time since the last resume to the current session.
    void stopClock();
    // Writes the current session's listening time so far (inserted once, then updated).
    void saveSession();
    // Saves and closes the current session; the next play starts a new one.
    void finishSession();

    static inline PlayerController *s_instance = nullptr;

    StatsDatabase &m_stats;
    AudioEngine m_engine;
    QTimer *m_pollTimer;
    QTimer *m_saveTimer;

    QList<QString> m_queue; // file paths
    QList<int> m_order;     // indices into m_queue in play order
    int m_orderPosition = -1;
    bool m_shuffle = false;
    Repeat m_repeat = RepeatOff;

    Track m_track;
    QList<Lyrics::Line> m_lyrics;
    int m_lyricIndex = -1;
    bool m_queueTried = false; // this track already asked the engine to queue the next one
    qint64 m_loopStartMs = -1;
    qint64 m_loopEndMs = -1;
    bool loopActive() const { return m_loopStartMs >= 0 && m_loopEndMs > m_loopStartMs; }
    void applyLoop(qint64 startMs, qint64 endMs);
    float replayGainFactor(const QString &path, const ReplayGain::Info &info) const;
    bool m_lyricsShown = true;
    double m_speed = 1;
    double m_reverb = 0;
    double m_pitchSemitones = 0;
    LyricsFetcher *m_lyricsFetcher;
    bool m_onlineLyrics = true;
    bool m_listenReported = false; // listenedLongEnough sent for this session
    qint64 m_scrobbleAfterMs = 15000;
    int m_crossfadeMs = 0;
    int m_replayGainMode = ReplayGain::Track;
    const LibraryDatabase *m_songGains = nullptr;
    ReplayGain::Info m_replayGain; // of the current track
    bool m_lyricsLoading = false;
    bool m_playing = false;
    qint64 m_positionMs = 0;
    double m_volume = 1.0;

    // Current listening session: wall-clock time actually spent playing,
    // so pauses and seeks never inflate it.
    QDateTime m_sessionStart;
    QElapsedTimer m_playClock;
    qint64 m_listenedMs = 0;
    qint64 m_playId = 0; // stats row of this session, 0 until first saved
};

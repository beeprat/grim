#include "controllers/DiscordController.h"
#include "controllers/LastFmController.h"
#include "controllers/SpotifyController.h"
#include "controllers/LibraryController.h"
#ifdef Q_OS_WIN
#include "controllers/MediaKeys.h"
#else
#include "controllers/Mpris.h"
#endif
#include "controllers/PlaylistController.h"
#include "controllers/PlayerController.h"
#include "controllers/SettingsController.h"
#include "controllers/SlskdController.h"
#include "controllers/StatsController.h"
#include "controllers/TrayController.h"
#include "controllers/UpdateController.h"
#include "controllers/WindowFrame.h"
#include "core/LibraryDatabase.h"
#include "core/LibrarySidecar.h"
#include "core/StatsDatabase.h"
#include "core/UserDataMigration.h"
#include "providers/CoverImageProvider.h"
#include "providers/WallpaperImageProvider.h"
#include "providers/LibraryModel.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#else
#include <QFont>
#include <QFontDatabase>
#endif

int main(int argc, char *argv[])
{
    // QApplication (not QGuiApplication): the tray icon's context menu is a widget.
    QApplication app(argc, argv);

    // One grim. per user: a second start (shortcut, "Open with") hands its file, if any, to the running one,
    // which comes to the front, and exits before touching any data.
    const QString instanceName = QStringLiteral("grimPlayer-")
        + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    const QString fileArgument = QCoreApplication::arguments().value(1);
    {
        QLocalSocket running;
        running.connectToServer(instanceName);
        if (running.waitForConnected(1000)) {
#ifdef Q_OS_WIN
            AllowSetForegroundWindow(ASFW_ANY); // we were just started by the user, so we may pass that on
#endif
            running.write(fileArgument.toUtf8());
            running.waitForBytesWritten(1000);
            running.disconnectFromServer();
            return 0;
        }
    }
    QLocalServer::removeServer(instanceName); // a crashed instance's leftover socket file (Linux)
    QLocalServer instanceServer;
    if (!instanceServer.listen(instanceName))
        qWarning("Cannot listen for other instances: %s", qPrintable(instanceServer.errorString()));
    // Organization/application names locate the settings (HKCU\Software\grimPlayer\grimPlayer), the
    // databases (%APPDATA%\grimPlayer\grimPlayer) and caches (%LOCALAPPDATA%\grimPlayer\grimPlayer).
    // Before 2026-10 they were "MusicPlayer"; that data is moved over once, on the first start.
    QApplication::setOrganizationName(QStringLiteral("MusicPlayer"));
    QApplication::setApplicationName(QStringLiteral("MusicPlayer"));
    const QString oldRoaming = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString oldLocal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QApplication::setOrganizationName(QStringLiteral("grimPlayer"));
    QApplication::setApplicationName(QStringLiteral("grimPlayer"));
    UserDataMigration::moveFolder(oldRoaming, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    UserDataMigration::moveFolder(oldLocal, QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    {
        QSettings oldSettings(QStringLiteral("MusicPlayer"), QStringLiteral("MusicPlayer"));
        QSettings newSettings;
        UserDataMigration::moveSettings(oldSettings, newSettings);
    }
#ifdef Q_OS_LINUX
    // Linux taskbars and trays label the app by its application name, so it is "grim" there:
    // ~/.local/share/grimPlayer/grim, ~/.cache/grimPlayer/grim, ~/.config/grimPlayer/grim.conf.
    // Before 0.3.2 it was "grimPlayer"; that data is moved over once, on the first start.
    {
        const QString oldData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const QString oldCache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QApplication::setApplicationName(QStringLiteral("grim"));
        UserDataMigration::moveFolder(oldData, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        UserDataMigration::moveFolder(oldCache, QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
        QSettings oldSettings(QStringLiteral("grimPlayer"), QStringLiteral("grimPlayer"));
        QSettings newSettings;
        UserDataMigration::moveSettings(oldSettings, newSettings);
    }
#endif
#ifdef Q_OS_WIN
    {
        // Drop the emptied old registry key, but only if nothing else lives under "MusicPlayer"
        // (another program could use that organization name).
        QSettings software(QStringLiteral(R"(HKEY_CURRENT_USER\Software)"), QSettings::NativeFormat);
        QSettings oldOrganization(QStringLiteral(R"(HKEY_CURRENT_USER\Software\MusicPlayer)"), QSettings::NativeFormat);
        const QStringList groups = oldOrganization.childGroups();
        const bool onlyOurs = groups.isEmpty() || groups == QStringList{QStringLiteral("MusicPlayer")};
        if (software.childGroups().contains(QStringLiteral("MusicPlayer")) && onlyOurs
            && oldOrganization.allKeys().isEmpty())
            software.remove(QStringLiteral("MusicPlayer"));
    }
#else
    // Windows draws the UI in Segoe UI; Linux gets Selawik, Microsoft's open-licence Segoe UI stand-in,
    // so text keeps the same look and metrics.
    for (const char *face : {"selawk", "selawksl", "selawkl", "selawksb", "selawkb"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/%1.ttf").arg(QLatin1String(face)));
    QApplication::setFont(QFont(QStringLiteral("Selawik"), 9));
    // Wayland matches the window to grim.desktop (icon, name in the dock).
    QGuiApplication::setDesktopFileName(QStringLiteral("grim"));
#endif
    // Shown name.
    QApplication::setApplicationDisplayName(QStringLiteral("grim."));
    QApplication::setApplicationVersion(QStringLiteral(GRIM_VERSION));
    const QIcon appIcon(QStringLiteral(":/qt/qml/MusicPlayer/qml/icons/app.svg"));
    QApplication::setWindowIcon(appIcon);

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    // Playback still works without stats, so a failed open is logged, not fatal.
    StatsDatabase stats;
    stats.open(dataDir + QStringLiteral("/stats.sqlite"));
    // Ratings, favorites, playlists. Like stats, the app still runs if it fails to open.
    LibraryDatabase libraryData;
    libraryData.open(dataDir + QStringLiteral("/library.sqlite"));

    // Declared before the engine so they outlive every QML binding that reads them.
    SettingsController settings(nullptr);
    // What a backup of the music folder carried over (another computer's ratings, favorites, playlists).
    LibrarySidecar::read(libraryData, settings.libraryFolder());
    StatsController statsController(stats);
    PlayerController playerController(stats);
    LibraryController library(settings, libraryData);
    statsController.setGenreLookup([&library](const QString &path) {
        const Track *track = library.findTrack(path);
        return track ? track->genre : QString();
    });
    PlaylistController playlists(libraryData, library);
    statsController.setLibraryData([&](const QString &folder) { return LibrarySidecar::toJson(libraryData, folder); },
                                   [&](const QJsonObject &doc, const QString &folder) {
                                       if (LibrarySidecar::merge(libraryData, doc, folder)) {
                                           emit library.metaChanged();
                                           emit playlists.playlistsChanged();
                                       }
                                   });
    // Keeps .grim-library.json in the music folder up to date, a moment after each change and on quit.
    QTimer sidecarWrite;
    sidecarWrite.setSingleShot(true);
    sidecarWrite.setInterval(2000);
    QObject::connect(&sidecarWrite, &QTimer::timeout, [&] { LibrarySidecar::write(libraryData, settings.libraryFolder()); });
    QObject::connect(&library, &LibraryController::metaChanged, &sidecarWrite, qOverload<>(&QTimer::start));
    QObject::connect(&playlists, &PlaylistController::playlistsChanged, &sidecarWrite, qOverload<>(&QTimer::start));
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
        if (sidecarWrite.isActive())
            LibrarySidecar::write(libraryData, settings.libraryFolder());
    });
    QObject::connect(&settings, &SettingsController::libraryFolderChanged, &library, [&] {
        if (LibrarySidecar::read(libraryData, settings.libraryFolder()))
            emit library.metaChanged();
    });
    SlskdController slskd(settings);
    SpotifyController spotify(library, playlists, slskd);
    QObject::connect(&slskd, &SlskdController::trackImported, &library, &LibraryController::addFile);
    QObject::connect(&slskd, &SlskdController::trackImported, &library, &LibraryController::addUnseen);
    slskd.setLibraryTracks([&library] { return library.tracks()->tracks(); });
    QObject::connect(&library, &LibraryController::aboutToDelete, &playerController, &PlayerController::releaseFiles);
    TrayController tray(settings, playerController, appIcon);
    UpdateController updates(QApplication::applicationVersion());
    playerController.setOnlineLyrics(settings.onlineLyrics());
    playerController.setCrossfadeMs(qRound(settings.crossfadeSeconds() * 1000));
    QObject::connect(&settings, &SettingsController::crossfadeSecondsChanged, &playerController,
                     [&] { playerController.setCrossfadeMs(qRound(settings.crossfadeSeconds() * 1000)); });
    playerController.setSongGains(&libraryData);
    // A song's own dB value lives with ratings; reapply it to the playing track.
    QObject::connect(&library, &LibraryController::metaChanged, &playerController, &PlayerController::refreshReplayGain);
    QObject::connect(&settings, &SettingsController::onlineLyricsChanged, &playerController,
                     [&] { playerController.setOnlineLyrics(settings.onlineLyrics()); });
#ifdef Q_OS_WIN
    MediaKeys mediaKeys(playerController);
#else
    Mpris mpris(playerController,
                QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/covers"));
#endif
    // Windows may end the process at shutdown/logoff before destructors run: save while it still asks.
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &playerController,
                     [&] { playerController.saveLastTrack(); }, Qt::DirectConnection);
    // Plays not yet accepted by Last.fm survive restarts in this file.
    LastFmController lastFm(dataDir + QStringLiteral("/scrobble-queue.json"), dataDir + QStringLiteral("/lastfm.txt"));
    playerController.setScrobbleAfterMs(LastFmController::kScrobbleAfterMs);
    QObject::connect(&playerController, &PlayerController::sessionStarted, &lastFm, &LastFmController::trackStarted);
    QObject::connect(&playerController, &PlayerController::listenedLongEnough, &lastFm,
                     &LastFmController::trackListened);
    // imgur links are kept with the user data (losing them means uploading covers again).
    DiscordController discord(playerController,
                              QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                  + QStringLiteral("/covers"),
                              dataDir + QStringLiteral("/imgur-covers.json"), dataDir + QStringLiteral("/discord.txt"));

    // "Open with" / command line: play the first argument; otherwise reload the last track, paused.
    if (!fileArgument.isEmpty())
        playerController.open(QUrl::fromLocalFile(fileArgument));
    else
        playerController.restoreLastTrack();

    WindowFrame windowFrame;
    QQmlApplicationEngine engine;
    // Engine takes ownership. Scaled covers are cached under the local (not roaming) cache folder.
    engine.addImageProvider(QStringLiteral("cover"),
                            new CoverImageProvider(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                                   + QStringLiteral("/covers")));
    engine.addImageProvider(QStringLiteral("wallpaper"), new WallpaperImageProvider);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    QObject::connect(&settings, &SettingsController::translationApplied, &engine, &QQmlEngine::retranslate);
    engine.loadFromModule("MusicPlayer", "Main");
    if (!engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        tray.setWindow(window);
        updates.check();
        QObject::connect(&instanceServer, &QLocalServer::newConnection, &app, [&] {
            QLocalSocket *other = instanceServer.nextPendingConnection();
            const auto handle = [&, other] {
                if (const QString file = QString::fromUtf8(other->readAll()); !file.isEmpty())
                    playerController.open(QUrl::fromLocalFile(file));
                tray.restoreWindow();
                other->deleteLater();
            };
            // The other start sends everything, then disconnects; it may already be gone by now.
            if (other->state() == QLocalSocket::UnconnectedState)
                handle();
            else
                QObject::connect(other, &QLocalSocket::disconnected, other, handle);
        });
#ifdef Q_OS_WIN
        mediaKeys.registerFor(window);
        windowFrame.attach(window);
        // Windows 11 rounds the frameless window's corners like normal windows (ignored on Windows 10).
        const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corners, sizeof(corners));
#else
        mpris.setWindow(window);
#endif
    }

    return QApplication::exec();
}

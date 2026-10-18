#include "controllers/DiscordController.h"
#include "controllers/LastFmController.h"
#include "controllers/LibraryController.h"
#include "controllers/MediaKeys.h"
#include "controllers/PlaylistController.h"
#include "controllers/PlayerController.h"
#include "controllers/SettingsController.h"
#include "controllers/SlskdController.h"
#include "controllers/StatsController.h"
#include "controllers/TrayController.h"
#include "core/LibraryDatabase.h"
#include "core/StatsDatabase.h"
#include "core/UserDataMigration.h"
#include "providers/CoverImageProvider.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>

#include <qt_windows.h>
#include <dwmapi.h>

int main(int argc, char *argv[])
{
    // QApplication (not QGuiApplication): the tray icon's context menu is a widget.
    QApplication app(argc, argv);
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
    // Shown name.
    QApplication::setApplicationDisplayName(QStringLiteral("grim."));
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
    StatsController statsController(stats);
    PlayerController playerController(stats);
    LibraryController library(settings, libraryData);
    PlaylistController playlists(libraryData, library);
    SlskdController slskd(settings);
    QObject::connect(&slskd, &SlskdController::trackImported, &library, &LibraryController::addFile);
    QObject::connect(&library, &LibraryController::aboutToDelete, &playerController, &PlayerController::releaseFiles);
    TrayController tray(settings, playerController, appIcon);
    playerController.setOnlineLyrics(settings.onlineLyrics());
    playerController.setCrossfadeMs(qRound(settings.crossfadeSeconds() * 1000));
    QObject::connect(&settings, &SettingsController::crossfadeSecondsChanged, &playerController,
                     [&] { playerController.setCrossfadeMs(qRound(settings.crossfadeSeconds() * 1000)); });
    QObject::connect(&settings, &SettingsController::onlineLyricsChanged, &playerController,
                     [&] { playerController.setOnlineLyrics(settings.onlineLyrics()); });
    MediaKeys mediaKeys(playerController);
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
    if (const QStringList args = QCoreApplication::arguments(); args.size() > 1)
        playerController.open(QUrl::fromLocalFile(args.at(1)));
    else
        playerController.restoreLastTrack();

    QQmlApplicationEngine engine;
    // Engine takes ownership. Scaled covers are cached under the local (not roaming) cache folder.
    engine.addImageProvider(QStringLiteral("cover"),
                            new CoverImageProvider(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                                   + QStringLiteral("/covers")));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    QObject::connect(&settings, &SettingsController::translationApplied, &engine, &QQmlEngine::retranslate);
    engine.loadFromModule("MusicPlayer", "Main");
    if (!engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        tray.setWindow(window);
        mediaKeys.registerFor(window);
        // Windows 11 rounds the frameless window's corners like normal windows (ignored on Windows 10).
        const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corners, sizeof(corners));
    }

    return QApplication::exec();
}

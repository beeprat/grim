# MusicPlayer

Native Windows (and Linux) music player shown as **grim.** (exe `GRIM.exe`). C++20, Qt 6 (>= 6.8), QML, CMake.
CMake target and QML module stay `MusicPlayer`. Settings page signature is a plain string, never `qsTr`.

- Display name via `QApplication::setApplicationDisplayName("grim.")`; User-Agent `GRIM/0.1`.
- Org/app name `grimPlayer`: QSettings `HKCU\Software\grimPlayer\grimPlayer`, data `%APPDATA%\grimPlayer\grimPlayer`,
  caches `%LOCALAPPDATA%\grimPlayer\grimPlayer\cache`. Before 2026-10 it was `MusicPlayer`; `core/UserDataMigration`
  moves that once at startup, never overwriting new data.

## Build

Toolchain: Qt `C:\Qt\6.10.3\msvc2022_64`, VS 2022 Build Tools (MSVC 14.44), Ninja (winget). Run from an x64 Native
Tools prompt or call `vcvars64.bat` first; put Ninja on PATH *before* vcvars (a later `set PATH=...;%PATH%` on the
same cmd line expands too early and drops the compiler).

```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.10.3\msvc2022_64"
cmake --preset debug; cmake --build --preset debug   # build/debug, POST_BUILD runs windeployqt
ctest --test-dir build/debug --output-on-failure
```

- QTest exit code = failed test count. Warnings: run the test exe with `-o out.txt,txt` (`QWARN` lines).
- Always use the Ninja presets (clangd needs `build/debug/compile_commands.json`).
- MSVC: `/wd4702` on the app target only (qmlcachegen output triggers C4702 in Qt headers).

Distribution (`dist/`): release preset, copy `GRIM.exe` to `dist/GRIM/`, then
`windeployqt --release --qmldir qml --no-translations --no-opengl-sw --no-system-d3d-compiler dist/GRIM/GRIM.exe`.
Delete: `dxcompiler.dll`, `dxil.dll`, `qmltooling/`, `generic/`, SQL drivers except `qsqlite.dll`, Controls styles
Fusion/Imagine/Universal/FluentWinUI3/Windows (dirs + DLLs; keep Material and Basic),
`qml/QtQuick/{NativeStyle,Particles,LocalStorage,VectorImage,tooling}`, `qml/QtQml/XmlListModel`. Copy the CRT
app-locally (`msvcp140*.dll`, `vcruntime140*.dll` from `VC/Redist/MSVC/<ver>/x64/Microsoft.VC143.CRT`).
Pack `7z a -t7z -mx=9 -md=64m -ms=on GRIM-win64.7z GRIM`; check `dumpbin /dependents` and run with only System32 on
PATH. Installer: `makensis installer\grim.nsi` (per-user, `%LOCALAPPDATA%\Programs\grim`) -> `dist/grim-setup.exe`.
`installer/grim.iss` is the older Inno version. Bump the version in `CMakeLists.txt`, `grim.nsi` and `grim.iss`.

## Project structure

- `src/{controllers,core,models,providers,audio}`, `qml/{components,pages}`, `tests/tst_<class>.cpp` (one exe per
  class, `add_test`).
- No `.qrc`: `qt_add_qml_module` lists every QML file and QML-registered C++ source. Other C++ goes into the
  `musicplayer_core` static library so tests can link it.
- Generated `*_qmltyperegistrations.cpp` includes headers by bare name: each `src/` folder holding them must be in
  `target_include_directories(MusicPlayer ...)`. QML in `components/`/`pages/` needs `import MusicPlayer`.
- Do not edit generated files: `build/` (incl. `_deps/`), `compile_commands.json`, moc/qrc/rcc output,
  `*_qmltyperegistrations.cpp`, `*.qmltypes`, `qmldir`, `.qmlls.ini`, `.cache/`. `CMakeUserPresets.json` is per-developer.

## Rules

- MVC: C++ owns state and logic; QML only displays and forwards intent (view state only).
- `src/audio/` includes no Qt Quick/QML. DSP on the audio thread: no allocation, locks, signals or logging; parameters
  via atomics. Never call `AudioEngine` from more than one thread.
- Network: one shared `QNetworkAccessManager`, async only; `reply->deleteLater()` in `finished`.
- Naming: `PascalCase` types, `camelCase` functions, `m_`/`s_` members, `kPascalCase` constants; one class per file;
  `#pragma once`; includes own header, Qt, std, third-party; `QStringLiteral`/`u"..."_s`.
- Every QObject has a parent or smart-pointer owner; `deleteLater()`, no naked delete. Objects returned to QML without
  a parent become JS-owned.
- QML registration is declarative (`QML_ELEMENT`/`QML_SINGLETON`). `Q_PROPERTY` has `NOTIFY` (or `CONSTANT`) and
  `FINAL`; setters compare before emitting. Lists are `QAbstractListModel` with `roleNames()`.
- Main-owned singletons must NOT be default-constructible, or the QML engine builds a second instance instead of
  calling `create()`. They get `static T *create(QQmlEngine *, QJSEngine *)` returning the `main.cpp` instance with
  `CppOwnership`.
- Delegates use `required property`.

## Pitfalls

- SQLite: `QSqlQuery::finish()` before `commit()` on any query that returned rows (PRAGMAs, `RETURNING`), or
  "SQL statements in progress". A null `QString` binds as `NULL`: use `COALESCE(?, '')`.
- QML bindings on `LibraryController.metaRevision` must keep the dependency: `metaRevision >= 0 ? rating(url) : 0`.
  The comma form `(metaRevision, rating(url))` loses it in compiled QML.
- Compare file URLs decoded and lower-cased (`decodeURIComponent(u.toString())`); models differ in encoding.
- Views bind `model:` to the view-mode settings, never to `visible` (false while the album page covers them, which
  resets the model and scroll position).
- A nested layout inherits `fillWidth` from children: fixed side columns need `Layout.fillWidth: false` + `maximumWidth`.
- Material's highlighted `Button` always has white text; use `AccentButton.qml` on light accents.
- moc: a `#...` line inside a C++ raw string makes moc write an empty `.moc`. Build test input from normal literals.
- `qsTr()`/`tr()` take exactly one string literal (the extractor sees only the first part of `"a" + "b"`); use `.arg()`.
- Paths to TagLib/miniaudio/FFmpeg go through `QFileInfo::filesystemFilePath()`, never `toStdWString()`.
- Tray minimize: hide the window through a queued call; a synchronous hide during minimize is undone by Windows.
- Windows will not delete open files: `LibraryController::aboutToDelete` makes `PlayerController::releaseFiles`
  unload them first.

## Data files

- `stats.sqlite` (`core/StatsDatabase`): schema version in `PRAGMA user_version`; `migrate()` has one step list per
  version. To change it, bump `kSchemaVersion` and append a step; never edit a shipped step. `listened_ms` is
  wall-clock playing time; `counted` = 30 s, or half of a track under a minute. Sessions under 1 s are not stored.
  Export/import JSON `format: grim-stats` v1; import skips plays with the same path + start time.
- `library.sqlite` (`core/LibraryDatabase`): ratings, favorites, per-song volume adjustment in dB
  (`replaygain_db`, NULL = none; schema v3), playlists, keyed by absolute path with forward slashes. Playlist id 0 is
  the built-in Favorites list. Older builds refuse a newer schema (no ratings shown) rather than downgrade it.
- `cache/library-index.bin` (`core/LibraryIndex`): bump `kVersion` when `Track` changes.
- `cache/covers` (`core/CoverCache`, key path + mtime + size, `.none` = no cover), `cache/artists` (Deezer photos),
  `cache/lyrics` (LRCLIB; `.none` retried after 7 days). Network failures are never cached. Safe to delete.
- Key files (`core/KeyFile`, `key = value`, `#` comments): `lastfm.txt` (`api_key`, `shared_secret`), `discord.txt`
  (`application_id`, `imgur_client_id`; empty values fall back to `kDefaultApplicationId`/`kDefaultImgurClientId`).
- `scrobble-queue.json`, `imgur-covers.json`, `translations/<Language>.txt` (`"English" = "Translated";`).
- QSettings: `player/volume|shuffle|repeat` and `player/last*` (queue, track, position) saved on quit and on
  `QGuiApplication::commitDataRequest` (Windows shutdown/logoff may kill the process before destructors run);
  `slskd/username|managedApiKey`, `slskd/downloadFormat`, `playback/crossfadeSeconds`, `library/*`,
  `layout/grid`. QML-side settings use QtCore `Settings` categories (`theme`, `effects`, `library`, `nowPlaying`).

## Audio

- `audio/AudioEngine`: miniaudio 0.11.25 (FetchContent, pinned URL + SHA-256; change both to upgrade), `ma_engine` +
  two `ma_sound` slots (current, fading/queued). Only `AudioEngine.cpp` includes `miniaudio.h`.
- Decoders: WAV/FLAC/MP3 via miniaudio; M4A/AAC via `AacSource` = `MfAudioSource` (Media Foundation) on Windows,
  `FfmpegAudioSource` on Linux. Decoding runs on the mixing thread (documented shortcut).
- Engine volume = user slider; per-sound volume is the song's own dB value (right-click → "Volume adjustment…",
  dialog in `Main.qml`, ±20 dB, `core/ReplayGain::fromDb`), 1 without one. ReplayGain tags are not applied (removed
  on request); the dialog only shows the tag as a hint. A gapless track gets its gain when queued; `LibraryController::metaChanged` reapplies it.
- Effects: only in the popup by the volume, and applied only while Settings → Effects → "Show effects button" is
  on (`Effects.showInNowPlaying`). Speed (0.5–1.5, pitch follows) = `ma_sound_set_pitch`; Reverb (Freeverb) in a custom
  `EffectsNode`, which also does Mono (Settings → Playback, `player/mono`: both channels = their average, after the
  reverb). Speed slider: marks at 80 % / 120 %, snaps within 1.5 %. Gapless timing and Discord timestamps divide by the speed. Speed is stored under the old
  `effects/slowed`, `effects/slowedSpeed` keys. Pitch shift (SoundTouch) was removed on request.
- `PlayerController` polls every 200 ms (15 ms while an A-B loop is set); `positionChanged` fires once per second.
- Crossfade: only automatic track changes (not manual next/prev, repeat one, or tracks shorter than two fades).
- Gapless (crossfade off): 5 s before the end, `queueNext` schedules the next track with
  `ma_sound_set_start_time_in_pcm_frames`; pause/seek/load/repeat/crossfade/order changes drop it, re-queued when due.
- A-B loop (Ctrl+click/drag on `SeekBar`, right-click clears): gapless and crossfade are off inside it; a new track
  clears it; not persisted.

## Integrations

- "Run slskd for me" (`core/SlskdRunner`, owned by `SlskdController`): downloads slskd 0.26.2 (pinned URL + SHA-256
  per platform, win-x64/linux-x64; change version and hashes together) to `%LOCALAPPDATA%/grimPlayer/grimPlayer/slskd/
  <version>`, unpacks with `QZipReader` (Qt6::CorePrivate), runs it hidden with `--app-dir .../slskd/data` (never the
  user's own slskd folder; `APP_DIR` env is ignored) and settings as `SLSKD_*` env vars: 127.0.0.1:5031, no HTTPS,
  auth on with a random key (`slskd/managedApiKey`, cidr 127.0.0.1/32). Dies with grim. (job object / PDEATHSIG);
  unexpected exits retried 3 times. Login: `slskd/username`, password via `core/CredentialStore` (Credential Manager
  `grimPlayer/slskd-password`; Linux 0600 file). Settings → Soulseek is only that login; nothing runs until a
  username is set. Connecting to a slskd the user runs themselves was removed on request. The title bar's
  start/stop button (`managedRunning`) is remembered in `slskd/stopped`; saving the login clears it.
- slskd (`core/SlskdClient`, API v0, `X-API-Key`, endpoints follow llehouerou/waves `client.go`): callbacks only on
  success, every failure once via `errorOccurred`. Status (`GET application`) is checked only at startup, after a
  failure and on click; no background polling. The API reports no local paths: find downloads by name under
  `directories.downloads` from `GET options`.
- Download import (`SlskdController::importToLibrary`, `core/DownloadTagger`): the title loses edition notes
  ("(Remastered 2009)", "[Deluxe Edition]", " - 2015 Remaster", "(50th Anniversary)": anniversary editions join the
  original album), and a leading track number leaves the file name (`cleanFileName`: only the tag's number or a
  zero-padded one, so "99 Problems" stays). If the library already has the album (same album and
  artist after `albumKey`/`artistKey`: case, "The ", punctuation and edition notes ignored), the file gets the folder
  most of those tracks are in, their most common artist / album name / year, and their most common cover, then moves
  there (`LibraryFiles::moveIntoFolder`); otherwise it goes in by its download folder as before. Library tracks come
  from `setLibraryTracks` (main.cpp).
- Download conversion (`core/AudioConverter`): ffmpeg from PATH, writes `<name>.converting.<ext>` (skipped by scans),
  copies tags with TagLib, then renames. Original goes to the Recycle Bin.
- Last.fm (`core/LastFmClient`): signature = md5 of sorted params (no `format`) + secret. Queue sent in batches of
  50, removed only once accepted; errors -1, 4, 10, 11, 13, 16, 26, 29 keep it, 9 disconnects but keeps it, others
  drop the batch. Scrobble after 15 s of actual listening.
- Discord (`core/DiscordIpc`, no SDK): pipe `discord-ipc-0..9` (Unix socket in `$XDG_RUNTIME_DIR` on Linux), frames
  op + int32 LE length + JSON. Updates debounced 1.5 s. Covers uploaded to imgur, keyed by sha1 of the JPEG.
- Lyrics: tag / sidecar `.lrc`, then cache, then LRCLIB (`/api/get`, then `/api/search`). `lyricIndex` updates only
  while the lyrics panel is visible (`PlayerController.lyricsShown`).
- Media keys: `RegisterHotKey` (`controllers/MediaKeys`) on Windows, `controllers/Mpris` on Linux. `MediaKeys` also
  adds the taskbar thumbnail buttons (previous / play-pause / next, `ITaskbarList3`), re-added on every
  `TaskbarButtonCreated` (first show, back from the tray, Explorer restart); clicks arrive as `WM_COMMAND`.
- Single instance: `QLocalSocket`/`QLocalServer` named `grimPlayer-<user>`; a second start hands over its file
  argument and exits before touching data.
- Update check: `controllers/UpdateController` asks `api.github.com/repos/beeprat/grim/releases/latest` once at
  startup and compares its tag with `GRIM_VERSION` (= CMake `PROJECT_VERSION`, so bump that per release); a newer one
  shows a pill in the middle of `TitleBar.qml`. Clicking it (`install()`) downloads the release's `grim-setup.exe`
  (only from `github.com/beeprat/grim/releases/download/`, must start with `MZ`) to TEMP, runs it with `/S` and quits;
  `grim.nsi` waits up to 10 s for `GRIM.exe` to be deletable, replaces the folder and (silent only) starts the new
  version. Copies outside `%LOCALAPPDATA%\Programs\grim` (7z, Linux) and failed downloads open the release page.
  No signature check: the trust is the same as downloading from the repo by hand. Check failures are silent.

## UI

- `qml/Theme.qml` singleton: use `Theme.*`, never literal colors. Panels use `Theme.panel` (the card color, 72 %
  opaque when a background image is set); `Theme.card`/`Theme.window` stay opaque for popups and icon colors.
  `themes` are presets of background + accent indexes (append to `accents`/`backgrounds`, never reorder: the
  indexes are stored). Background image: `SettingsController::importBackground` copies it to
  `%APPDATA%/grimPlayer/grimPlayer/background-<ms>.<ext>`; `Main.qml`'s window background blurs it (`MultiEffect`, `Theme.backgroundBlur` 0..1 from the Blur slider; `Theme.backgroundFill` is the `Image.fillMode`:
  crop, fit or stretch). `Theme.panel` is see-through then: never overlap two panel-colored items (`tabBridge` in
  `PlaylistsPage` meets its row and the tracks card edge to edge).
- Menus: `CompactMenu`/`CompactMenuItem`/`CompactComboBox` with `PopupBackground`. One `TrackContextMenu` in `Main`,
  opened via `ApplicationWindow.window.openTrackMenu([urls])`.
- Pages: 0 Library, 1 Playlists, 2 Statistics, 3 Soulseek, 4 Settings. Non-Library pages are `LazyPage` loaders.
- Panel grid (`Main.qml`): empty `layout/grid` = classic pixel layout; rects are [col, row, w, h, pxHeight, pxWidth]
  (-1 = scales), see `placeAxis()`/`flexAxis()`.
- Frameless window: `TitleBar.qml` (drag, buttons, slskd dot), `ResizeEdge.qml`; DWM rounded corners on Windows 11.
  The title bar is outside the Ctrl+wheel zoom area.
- Track order inside an album: disc, track number, file name (`albumOrderLess`). `LibraryModel::tracks()` stays in
  artist order (`AlbumsModel` and `findTrack` index into it).

## Linux

- Built only on GitHub Actions (`.github/workflows/linux-appimage.yml`, public repo `beeprat/grim`): Ubuntu 22.04,
  Qt 6.10.3, g++-12, static audio-only FFmpeg 7.1.2 (cached), tests offscreen, linuxdeploy -> `grim-x86_64.AppImage`.
  A `v*` tag publishes a release. Nothing Linux-specific compiles on Windows.
- The workflow copies Qt's `plugins/wayland-*` folders, or Qt Quick aborts on Wayland. Only `qsqlite` is kept.
- `main.cpp` loads Selawik fonts (`resources/fonts`) as the Segoe UI stand-in. Window corners stay square.

## Planned

libebur128 (ReplayGain scan), via `find_package`/`FetchContent` when the feature lands.

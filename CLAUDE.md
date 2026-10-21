# MusicPlayer

Native Windows music player, shown to users as **grim.** (exe `GRIM.exe`). The Settings page ends with the signature "grim. - GRIMSIXSTAR, šarić. - 2026. - 1.0", a plain string on purpose (not `qsTr`, never translated). C++20, Qt 6 (>= 6.8), QML UI, CMake.

The visible name comes from `QApplication::setApplicationDisplayName("grim.")` (window title, title bar,
tray) and the network User-Agent (`GRIM/0.1`). The exe is `GRIM.exe` (`OUTPUT_NAME`). The CMake target and QML module stay `MusicPlayer`.
The organization/application names are `grimPlayer` (QSettings `HKCU\Software\grimPlayer\grimPlayer`,
`%APPDATA%\grimPlayer\grimPlayer`, `%LOCALAPPDATA%\grimPlayer\grimPlayer`). Until 2026-10 they were `MusicPlayer`;
`main.cpp` moves that data once at startup (`core/UserDataMigration`: folders renamed, registry keys copied
then cleared, never overwriting existing new data; tested in `tests/tst_userdatamigration.cpp`).

## Build

Installed toolchain on this machine:
- Qt 6.10.3 MSVC kit: `C:\Qt\6.10.3\msvc2022_64` (installed with `aqtinstall`)
- VS 2022 Build Tools (MSVC 14.44): `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`
- Ninja (winget): `%LOCALAPPDATA%\Microsoft\WinGet\Links`

Run from an "x64 Native Tools Command Prompt for VS 2022" so `cl.exe` is on PATH, or call
`vcvars64.bat` first. Put Ninja on PATH *before* calling vcvars; a later `set PATH=...;%PATH%`
on the same cmd line expands too early and drops the compiler.

```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.10.3\msvc2022_64"
cmake --preset debug          # configure -> build/debug, writes compile_commands.json
cmake --build --preset debug  # build
.\build\debug\GRIM.exe   # or double-click it
```

A POST_BUILD step runs `windeployqt` on every build, so `build/<preset>/` holds the Qt DLLs,
plugins and QML modules and the exe starts without Qt on PATH.

Tests (Qt Test, run through CTest): `ctest --test-dir build/debug --output-on-failure`.
QTest's exit code is the number of failed tests. For warning text, run the test exe with
`-o out.txt,txt`; it prints `QWARN` lines there.

Release: `cmake --preset release && cmake --build --preset release`.
Deploy Qt DLLs next to the exe: `windeployqt --qmldir qml build\release\GRIM.exe`.

Distribution package (`dist/`, separate from `build/debug`): build the release preset, copy
`build/release/GRIM.exe` to `dist/GRIM/`, then
`windeployqt --release --qmldir qml --no-translations --no-opengl-sw --no-system-d3d-compiler dist/GRIM/GRIM.exe`.
Then trim what the app never loads: `dxcompiler.dll`/`dxil.dll` (Direct3D 12 only; Qt Quick uses D3D11),
`qmltooling/` (QML debugger), `generic/` (TUIO touch), SQL drivers except `qsqlite.dll`, the Controls styles
Fusion/Imagine/Universal/FluentWinUI3/Windows (dirs under `qml/QtQuick/Controls` + their DLLs; the app uses only
Material, which needs Basic), and `qml/QtQuick/{NativeStyle,Particles,LocalStorage,VectorImage,tooling}`,
`qml/QtQml/XmlListModel`. Copy the C++ runtime app-locally (`msvcp140.dll`, `msvcp140_1.dll`, `msvcp140_2.dll`,
`vcruntime140.dll`, `vcruntime140_1.dll` from `VC/Redist/MSVC/<ver>/x64/Microsoft.VC143.CRT`) instead of
shipping `vc_redist.x64.exe`. Pack with `7z a -t7z -mx=9 -md=64m -ms=on GRIM-win64.7z GRIM`.
Check with `dumpbin /dependents` that every DLL still resolves, and run it with only System32 on PATH.
Installer: `makensis installer\grim.nsi` (NSIS 3, per-user into `%LOCALAPPDATA%\Programs\grim`) -> `dist/grim-setup.exe`.
`installer/grim.iss` is the older Inno Setup version of the same installer.

clangd: always use the Ninja presets. The Visual Studio generator does not emit
`compile_commands.json`. clangd finds `build/debug/compile_commands.json` on its own;
if not, add `.clangd` with `CompileFlags: { CompilationDatabase: build/debug }`.

## Layout

```
src/main.cpp          app entry, creates QQmlApplicationEngine
src/controllers/      QML-facing controllers (QObject, QML_ELEMENT / QML_SINGLETON)
src/core/             database (SQLite stats), network (slskd REST), settings
src/models/           Track, Album, Playlist; QAbstractListModel subclasses
src/providers/        QML data providers (adapt models for views)
src/audio/            audio engine, DSP (slowed + reverb), ReplayGain
qml/Main.qml          root window
qml/components/       reusable QML components
qml/pages/            full pages (Library, Statistics, Soulseek)
```

```
tests/tst_<class>.cpp one Qt Test executable per tested class, registered with add_test
```

All QML files and QML-registered C++ sources are listed in `qt_add_qml_module`
in `CMakeLists.txt`. There is no hand-written `.qrc`; the module generates it.
New QML file -> add to `QML_FILES`. New C++ class -> add to `SOURCES` of
`qt_add_qml_module` if exposed to QML; otherwise add it to the `musicplayer_core`
static library so tests can link it.
The generated `*_qmltyperegistrations.cpp` includes QML-registered headers by bare file name,
so each `src/` folder holding them must be in `target_include_directories(MusicPlayer ...)`.
QML files in `qml/components/` and `qml/pages/` need `import MusicPlayer` to see each other
and the C++ singletons.

## Statistics database

`src/core/StatsDatabase` owns `%APPDATA%\grimPlayer\grimPlayer\stats.sqlite` (created in `main.cpp`).
- Schema version lives in `PRAGMA user_version`. `migrate()` holds one statement list per step
  (0->1 creates tables, 1->2 recounts `counted` under the 30 s rule). To change the schema, bump
  `kSchemaVersion` and append a step. Never edit an already-shipped step.
- Tables: `tracks` (keyed by unique `path`) and `plays` (one row per listening session).
  `listened_ms` is wall-clock time actually playing, so pauses and seeks don't count. `counted = 1` once
  `countsAsPlay()` passes: 30 s, or half of a track shorter than a minute.
- `PlayerController` saves the running session every 5 s through `recordPlay(..., &playId)`: the first
  call inserts, later calls update. Sessions under 1 s are not stored.
- Rankings (top tracks/artists/albums) order by listening time; play counts are shown alongside.
  Albums group by album name + artist.
- Statistics Export / Import (small buttons in the title bar next to the slskd status, shown on the Statistics
  page; dialogs in `TitleBar.qml`): `exportJson()` writes every play as JSON (`format: grim-stats`, version 1,
  plays with path, tags, startedAt, listenedMs, counted); `importJson()` adds them in one transaction, skipping
  plays already present (same path + start time), so re-importing is harmless. Known tracks keep their tags.
- Call `QSqlQuery::finish()` before `commit()` on any query that returned rows
  (PRAGMAs, `RETURNING`), or SQLite fails with "SQL statements in progress".
- Bind optional text through `COALESCE(?, '')`: a null `QString` binds as SQL `NULL`.

## Architecture rules

- MVC: C++ owns state and logic, QML only displays and forwards user intent.
  No business logic in QML beyond view state (selected tab, hover, animations).
- `src/audio/` must not include any Qt Quick / QML header. Controllers adapt it for QML.
- DSP code runs on the audio thread: no allocation, no locks, no QObject signals,
  no logging inside the per-buffer process path. Pass parameters via atomics.
- Network calls (slskd) use one shared `QNetworkAccessManager`, async only; never block the GUI thread.

## C++ conventions

- Classes, structs, enums: `PascalCase`. Methods, functions, variables: `camelCase`.
- Private members: `m_` prefix (`m_volume`). Statics: `s_` prefix. Constants: `kPascalCase`.
- One class per file: `src/<area>/TrackModel.h` + `TrackModel.cpp`, file name = class name.
- `#pragma once`. Include order: own header, Qt, std, third-party.
- Use `QStringLiteral` / `u"..."_s` for string literals.
- Prefer `std::` types inside `src/audio/`; Qt types at QML boundaries.

## Memory management

- Every `QObject` gets a parent or is owned by a smart pointer. No naked `delete` on QObjects;
  use `deleteLater()` for objects that may still have queued events.
- Non-QObject resources: `std::unique_ptr`. Never mix parent ownership and `unique_ptr` on one object.
- Objects returned to QML from `Q_INVOKABLE` without a parent become JS-owned and are garbage
  collected. Give them a parent or call `QJSEngine::setObjectOwnership(obj, QJSEngine::CppOwnership)`.
- `QNetworkReply`: always `reply->deleteLater()` in its `finished` handler.

## QML / C++ integration

Declarative registration only (no `qmlRegisterType` calls):

```cpp
#include <QObject>
#include <QtQml/qqmlregistration.h>

class PlayerController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged FINAL)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)

public:
    explicit PlayerController(QObject *parent = nullptr);

    bool playing() const;
    double volume() const;
    void setVolume(double volume);   // emit volumeChanged only if value changed

    Q_INVOKABLE void togglePlay();

signals:
    void playingChanged();
    void volumeChanged();

private:
    bool m_playing = false;
    double m_volume = 1.0;
};
```

- Every `Q_PROPERTY` has a `NOTIFY` signal unless `CONSTANT`. Mark `FINAL`.
- Setters compare before assigning and emitting.
- Main-owned singletons must NOT be default-constructible (no `parent = nullptr` default when the
  constructor has no other parameter). Otherwise the QML engine constructs its own second instance
  instead of calling `create()`; `SettingsController(QObject *parent)` is written that way on purpose.
- Singletons needing constructor arguments: add
  `static PlayerController *create(QQmlEngine *, QJSEngine *)` returning an instance created in
  `main.cpp`, and set `CppOwnership` on it so the engine does not delete it.
- QML usage: `import MusicPlayer` then `PlayerController.togglePlay()`.
- Lists exposed to QML are `QAbstractListModel` subclasses with `roleNames()`, not `QVariantList`.
- QML delegates use `required property`; avoid unqualified access to outer ids where possible.

## Do not touch (generated)

- `build/` and everything in it, including fetched third-party sources in `build/<preset>/_deps/`
- `compile_commands.json`
- `moc_*.cpp`, `qrc_*.cpp`, `*.rcc`, `ui_*.h`
- `*_qmltyperegistrations.cpp`, `*.qmltypes`, generated `qmldir`
- `.qmlls.ini`, `.cache/` (clangd index)
- `CMakeUserPresets.json` (per-developer, not generated but not shared)

## Audio engine

- `src/audio/AudioEngine` wraps miniaudio 0.11.25 (`ma_engine` + two `ma_sound` slots: the
  current track and one still fading out).
  It is plain C++ with no Qt; `miniaudio.h` is included only in `AudioEngine.cpp`.
- miniaudio comes from `FetchContent` (pinned URL + SHA-256) into `build/<preset>/_deps/`.
  Only `miniaudio.c` is compiled, into our own `miniaudio` target. To upgrade, change the URL
  and hash together.
- Decoders: WAV, FLAC, MP3 via miniaudio (streamed on its job thread); M4A/MP4/AAC via
  `src/audio/MfAudioSource` (Windows Media Foundation source reader, float PCM) played with
  `ma_sound_init_from_data_source`. MF decoding runs on the mixing thread (documented shortcut).
  `musicplayer_core` links mfplat/mfreadwrite/mfuuid and defines NOMINMAX + WIN32_LEAN_AND_MEAN.
- Threads: miniaudio decodes on its resource-manager thread and mixes on the device thread.
  `seek`/`play` from the GUI thread are safe: miniaudio defers the seek to the mixer.
  Never call `AudioEngine` from more than one thread.
- Volume split: `AudioEngine::setVolume` is the master (`ma_engine`) volume for the user's slider.
  The per-sound volume (`ma_sound_set_volume`) is reserved for ReplayGain gain.
- Effects (Settings → Effects; state in the `qml/Effects.qml` singleton, QtCore `Settings` category `effects`,
  bound to `PlayerController.speed/reverb/pitchSemitones`; the rows are `EffectsControls.qml`, also shown in a
  popup from a "tune" button left of the volume (`VolumeControl.qml`) when `Effects.showInNowPlaying` is on): Slowed = `ma_sound_set_pitch` on every sound (resampling: slower and lower, 70–95 %).
  Reverb (`audio/Reverb`, Freeverb, public domain) and Pitch (`audio/PitchShifter` over SoundTouch 2.3.3,
  LGPL-2.1, FetchContent with pinned hash, library sources only, float + SSE; ±12 semitones, tempo kept) run
  in one custom `ma_node` (`EffectsNode` in `AudioEngine.cpp`) between all sounds and the endpoint. Settings
  reach the mixing thread as atomics; off effects are skipped. Gapless scheduling and Discord timestamps divide
  by the speed. Tested in `tests/tst_effects.cpp` (pitch measured by zero crossings, reverb tail).
- shortcut: SoundTouch allocates inside putSamples on the mixing thread while its buffers grow (only the first
  blocks after pitch is turned on); move it to a worker thread with a ring buffer if that ever glitches.
- `PlayerController` owns the engine and polls it every 200 ms for position and end of track.
  It records one listening session per track load into `StatsDatabase`: wall-clock time
  spent playing, written on end of track, on opening another track, or on quit.

## Tags

- `src/core/TagReader::read()` uses TagLib 2.3.2 (static, `FetchContent`, pinned hash, no zlib).
  It never fails: if a file is untagged or unreadable, the title falls back to the file name.
- TagLib's build-tree `tag` target has no include dirs; `CMakeLists.txt` adds
  `taglib/`, `taglib/toolkit/` and the binary dir (for `taglib_config.h`).
- Pass paths as `QString::toStdWString().c_str()`: `TagLib::FileName` is `wchar_t` on Windows.
- `PlayerController` prefers the decoder's duration over TagLib's (VBR MP3 estimates).

## slskd (Soulseek)

- `src/core/SlskdClient`: async REST client (API v0, `X-API-Key` header) on one
  `QNetworkAccessManager`. Endpoints follow llehouerou/waves `internal/slskd/client.go`:
  `POST searches`, `GET searches/{id}`, `GET searches/{id}/responses`,
  `POST transfers/downloads/{username}` (body `[{filename,size}]`), `GET transfers/downloads`.
- Callbacks run only on success. Every failure goes out once through `errorOccurred`.
  `SlskdController` stops all polling on an error, so a stopped slskd shows one message.
- JSON parsing (`Slskd::parse*`) and filtering (`Slskd::matches`) are free functions,
  unit-tested in `tests/tst_slskdclient.cpp`. That test also runs a fake HTTP server to check
  request paths, headers and bodies; there is no live slskd in the test environment.
- Search: poll state every 1 s and refetch responses only when `responseCount` changes.
  Late replies with fewer files than the current list are dropped (responses only grow).
- Downloads: poll every 1.5 s while any transfer is not finished (state not `Completed*`).
- Grouping: `Slskd::groupByAlbum` keys albums by user + folder, folding CD1/CD2 disc folders into the
  album. `Slskd::namesFromFolder` guesses the artist from the parent folder, or from
  "Artist - Album" under generic folders. `SearchGroupsModel` shows albums or artists built from
  the *filtered* results, so a format filter makes "FLAC albums". `downloadAlbum` queues a whole
  album in one request.
- Settings (`slskd/serverUrl`, `slskd/apiKey`) are in `QSettings`; the API key is stored in plain text.
- The API key is optional. Without one, slskd must run with auth disabled
  (`--no-auth`, `SLSKD_NO_AUTH=true`, or `web.authentication.disabled: true`).
- Status: `SlskdClient::fetchStatus` asks `GET /api/v0/application` only at startup (5 tries, 2 s apart,
  stopping once Online), after any failed request, and when the title bar status is clicked
  (`recheckStatus()`); no background polling. Clicking it no longer opens the Soulseek page. A 401/403 reply
  means `AuthRequired`, no answer means `Offline`, and `server.isLoggedIn` decides `NotLoggedIn` or `Online`.
  It never emits `errorOccurred`. Search is enabled only when `Online`.

## Playback queue

- Crossfade (Settings → Playback, `playback/crossfadeSeconds`, 0–12 s in tenths, default 0 = off): `poll()`
  starts the next track once the current one has that much left. `AudioEngine::load(file, ms)`
  fades the old sound out with `ma_sound_stop_with_fade_in_milliseconds` and the new one in;
  `update()` frees the old slot once it stops. Only automatic changes crossfade: not manual
  next/previous, not repeat one, not tracks shorter than two fades. Pause cuts the fading track.
  Tested in `tests/tst_audioengine.cpp` (skips without an output device).
- Gapless (crossfade off): 5 s before the end, `PlayerController::queueNextIfDue` has `AudioEngine::queueNext`
  open the next track in the second slot and schedule it with `ma_sound_set_start_time_in_pcm_frames` at
  engine time now + remaining frames (engine clock and cursor read between two mixes), so it starts on the
  frame the current one ends. `poll()` then `promoteQueued()`s it without reloading. Pause, seek, load, a
  repeat or crossfade change and any order change (`rebuildOrder`) drop the queued track; it is re-queued
  when due. Not for repeat one or the wrap-around of repeat all, or tracks of unknown length.
  Tested in `tests/tst_audioengine.cpp`.
- A-B loop (`PlayerController.loopStartMs/loopEndMs`, -1 = unset; `setLoopStart/setLoopEnd/clearLoop`): `SeekBar.qml`
  Ctrl+press marks the start (drag fine-tunes, committed on release), the next Ctrl+press the end (swapped if
  earlier; jumps into the loop); a Ctrl+press on a complete loop starts a new one; right-click clears. Drawn as an
  accent band with end markers, elapsed time in accent. `poll()` seeks back to the start at the end (or when the
  track runs out) and polls every 15 ms while a loop is set; gapless queueing and crossfade are off inside a loop.
  A new track clears it; not saved across restarts.
- On quit `PlayerController` saves volume, shuffle and repeat (`player/volume|shuffle|repeat`, loaded in its
  constructor) and the queue, track and position (`player/last*`); `main.cpp` calls
  `restoreLastTrack()` (paused, at that position) unless a file was passed on the command line.
- Track rows (library list, album page, playlists) call `playTrack(urls, index)`: clicking the loaded track
  pauses/resumes it instead of restarting it. Play buttons still use `playQueue`.
- `PlayerController::playQueue(urls, start)` replaces the queue. Shuffle reorders it with the current
  track first. Repeat cycles Off → All → One. `previous()` restarts the track after 3 s.
- At the end of a track: repeat one replays it; otherwise it moves to the next track, wrapping only
  with repeat all.

## Lyrics

- `src/core/Lyrics`: `read()` takes the `LYRICS` / `UNSYNCEDLYRICS` tag via TagLib's PropertyMap
  (ID3 USLT, MP4 ©lyr, Vorbis LYRICS), or else a sidecar `<name>.lrc`. ID3 SYLT frames are not read.
  `parse()` handles LRC: several timestamps per line, `[offset:±ms]`, and drops `<mm:ss.xx>` word
  timings. Text without timestamps comes back unsynced (`timeMs = -1`). Tested in `tests/tst_lyrics.cpp`.
- `PlayerController` loads lyrics with the track and updates `lyricIndex` on every poll and seek.
  `NowPlayingPanel` keeps the current line a third of the way down, and clicking a line seeks.
  Lyrics are their own panel (`LyricsPanel.qml`, panel id `lyrics` on the layout grid); the lyrics icon on the
  now-playing panel shows and hides it (`Main.lyricsVisible`, QtCore `Settings` category `nowPlaying`). Layouts
  saved before the split give lyrics the lower ~45 % of the old now-playing rectangle.
- Panel grid default: until a panel is moved (`layout/grid` empty, `Main.classicLayout`), panels use
  `classicRect()`: the exact pre-grid layout in pixels (side column 240-300 px, rating/volume 52, seek/controls 64,
  now playing sized by `NowPlayingPanel.fixedCoverSide` = 45 % / 70 % of the column like before). Entering
  arrange mode converts it to exactly the same rectangles in fractional cells (`cellsFromClassic()`), so
  nothing moves or grows; dragging snaps edges to grid lines and other panels' edges (`snapMove`/
  `snapResize`). Minimum sizes are in pixels (`panelMinPixels`). Reset returns to the classic layout.
  Rects are [col, row, w, h, pxHeight, pxWidth]: bars (seek, controls, rating, volume) keep their pixel height
  and the side cards (`sideWidths`) their pixel width when the window is resized (`placeAxis()` per axis:
  a fixed panel keeps its pixel distance from the edge it was nearer to, using `layout/gridSize` saved with
  the layout; scaling panels' edges follow fixed neighbours), so a layout made in a maximized window does not
  squash in a smaller one. -1 = scales. If the fixed sizes no longer fit along an axis (overlap, off the grid or below
  `panelMinPixels`, e.g. a window snapped to half the screen), `flexAxis()` keeps the fixed panels on the
  window edges and fits everything between them into the remaining space in proportion; only if that also
  fails does the axis scale with the window (`axisFits()` checks each try).
  The arrange button is shown only with Settings → Experimental → "Arrange panels" on (`Theme.arrangePanels`).
  The arranged layout applies to the Library page only (`classicLayout` is true on other pages; the arrange
  button is shown only on Library).
- `positionChanged` fires once per second (seek bar redraws); the poll still runs every 200 ms and
  `lyricIndex` follows the exact position, but only while the lyrics card is shown and the window is
  neither in the tray nor minimized (`PlayerController.lyricsShown`, bound in `Main.qml`); it
  catches up when shown again.

## Ratings, favorites, playlists

- `src/core/LibraryDatabase` (`library.sqlite`, separate from stats) stores `track_meta` (rating 0–5,
  favorite), `playlists` and `playlist_tracks`, keyed by absolute path with forward slashes. Ratings
  and favorites are cached in memory on open. Playlist positions stay dense (remove shifts the
  rows after it). Tested in `tests/tst_librarydatabase.cpp`.
- `LibraryController.rating/setRating/isFavorite/toggleFavorite(url)` plus `metaRevision`. Bindings must
  read the revision in a way the QML compiler keeps, e.g. `metaRevision >= 0 ? rating(url) : 0`.
  A comma expression `(metaRevision, rating(url))` loses the dependency in compiled QML, so it never
  updates.
- `PlaylistController`: id 0 is the built-in Favorites list (hearted tracks, newest first); other
  ids are real playlists. Track tags come from `LibraryController::findTrack()`; a file outside
  the library shows its file name.
- Album right-click → "Delete album…" (`openTrackMenu(urls, true)`), confirmed in a dialog in `Main`:
  `LibraryController::deleteAlbum` emits `aboutToDelete` (`PlayerController::releaseFiles` unloads the playing or
  gapless-queued file and drops them from the queue; Windows will not delete open files), then
  `LibraryFiles::deleteAlbum` moves the files to the Recycle Bin, plus each folder left without music (never the
  library root or anything outside it). Playlist entries and ratings of deleted files are kept. Tested in
  `tests/tst_libraryfiles.cpp` with a plain delete instead of the Recycle Bin.
- One `TrackContextMenu` lives in `Main`. Pages open it with
  `ApplicationWindow.window.openTrackMenu([urls])` (an album passes all its tracks).
  "Open containing folder" runs `explorer.exe /select,"path"` via `setNativeArguments`.
- `NewPlaylistWindow.qml` is a separate modal window over `LibraryController.pickerTracks`: a second
  LibraryModel with its own search, so the library page's search is untouched.
- Soulseek "Clear downloads" calls `DELETE /api/v0/transfers/downloads/all/completed`, which removes
  only finished transfers.
- Sidebar pages: 0 Library, 1 Playlists, 2 Statistics, 3 Soulseek, 4 Settings. The Soulseek button is greyed
  out and disabled until slskd answers (`Main.slskdDetected`: status not Offline/Checking); if slskd goes away
  while that page is open, the library opens.
- Playlist rows: the album name links to its album page (`showAlbumOf(url, true)`).
- The playlists card's right edge drags to resize it (140 px to half the page; `playlists/listWidth`). Dragged under ~106 px it snaps to a 72 px
  strip: only the + button and playlist icons, names as tooltips. The playlists card is a shade darker than
  `Theme.card`; the open playlist's row uses `Theme.card` and `tabBridge` joins it to the tracks card like a tab.

## Online lyrics and keyboard

- `src/core/LyricsFetcher` looks up LRCLIB (`https://lrclib.net`, no key): `/api/get` (artist,
  title, album, duration) first, then `/api/search`. It prefers synced lyrics of about the same
  length. Answers are cached in `%LOCALAPPDATA%/grimPlayer/grimPlayer/cache/lyrics` (`.lrc`, or
  `.none` = not found, retried after 7 days). Network failures are not cached. It sends a
  `MusicPlayer/...` User-Agent. Tested with `tests/FakeHttpServer.h` (shared with the slskd test).
- `PlayerController::loadLyrics()`: file tag / sidecar first, then the cache, then online (only
  when Settings → "Look up lyrics online" is on, the default). Late answers for a track that is no
  longer playing are dropped.
- `controllers/MediaKeys`: `RegisterHotKey` on the main window for the media keys (play/pause,
  next, previous, stop), handled in a native event filter (`WM_HOTKEY`). A key another program
  already holds is logged and left alone. The QML `Shortcut`s in Main.qml cover it while the
  window has focus. Space toggles play/pause; focused text fields keep their spaces.

## Last.fm scrobbling

- `src/core/LastFmClient`: Scrobbling API 2.0 (`https://ws.audioscrobbler.com/2.0/`, POST form,
  `format=json`). Signature = md5 of params sorted by name (name+value, `format` excluded) + secret.
  Desktop auth: `auth.getToken` → browser `last.fm/api/auth/?api_key&token` → `auth.getSession`.
- Scrobbles go through a persistent queue (`%APPDATA%/grimPlayer/grimPlayer/scrobble-queue.json`),
  sent in batches of 50 and removed only once accepted. Errors -1 (network), 11, 16, 29 and the
  key/signature errors (4, 10, 13, 26) keep the queue; error 9 (session revoked) disconnects but keeps
  it; any other error drops that batch so it cannot block the queue. Tested in `tests/tst_lastfmclient.cpp`.
- `controllers/LastFmController` (Settings → Last.fm: connect/finish/disconnect only). The user's own API key +
  shared secret are in `%APPDATA%/grimPlayer/grimPlayer/lastfm.txt` (`api_key`, `shared_secret`; `core/KeyFile`),
  created on first start from the old `lastfm/apiKey`/`apiSecret` registry values and re-read on "Connect
  account". The session (`lastfm/user`, `lastfm/sessionKey`) stays in QSettings with `lastfm/sessionApiKey`; a
  different key in the file ends the session. `PlayerController` emits
  `sessionStarted` (now playing) and `listenedLongEnough` once per session after 15 s of actual
  listening (`kScrobbleAfterMs`), with the session start as the timestamp. Tracks without artist or
  title are not sent.

## Discord Rich Presence

- No SDK: `src/core/DiscordIpc` talks to the desktop client's pipe (`discord-ipc-0..9` via
  `QLocalSocket`): frames are op + length (int32 LE) + JSON. Handshake op 0 `{v:1, client_id}`, wait for
  the READY dispatch, then `SET_ACTIVITY` (op 1, `args.pid`, no `activity` key = clear). Answers pings,
  retries every 15 s while Discord is closed, resends the last activity after a reconnect.
- `controllers/DiscordController` (Settings → Discord: only the on/off switch, `discord/enabled` in QSettings,
  off by default, plus an error line). Defaults: grim.'s own application ID and imgur Client-ID
  (`kDefaultApplicationId`/`kDefaultImgurClientId`, used for new files and for empty values). The user's own Discord application ID (its name is the app name on
  Discord) and imgur Client-ID are in `%APPDATA%/grimPlayer/grimPlayer/discord.txt` (`application_id = ...`,
  `imgur_client_id = ...`, `#` comments; `core/KeyFile`), created on first start from the old `discord/*`
  registry values and re-read each time presence is turned on.
  Activity: `type: 2` (Listening), `status_display_type: 1` so the status reads "Listening to <artist>",
  details = title, `large_text` = "on <album>", start/end timestamps. Paused = presence cleared. Updates are
  debounced 1.5 s (Discord's rate limit); a seek is detected as a >3 s jump in the computed start time.
- Covers: `CoverCache` at 512 px → JPEG → `ImgurUploader` (anonymous `POST api.imgur.com/3/image`,
  `Authorization: Client-ID`). Keyed by sha1 of the JPEG, so an album's tracks share one upload.
  Links + deletehash live in `%APPDATA%/grimPlayer/grimPlayer/imgur-covers.json`; failures are not
  cached. The link goes into `large_image`. Tested in `tests/tst_discord.cpp` (fake pipe + FakeHttpServer).

## Translations

- `src/core/StringsTranslator` (a QTranslator) reads `<Language>.txt` files from
  `%APPDATA%/grimPlayer/grimPlayer/translations`: one `"English" = "Translated";` per line,
  with `\" \ 
` escapes. Lookups ignore the context (one line per English text). Missing or
  empty lines stay English. Tested in `tests/tst_stringstranslator.cpp`.
- `cmake/ExtractUiStrings.cmake` runs at build time over every `qml/*.qml` and `src/*.cpp`.
  It collects `qsTr("...")` / `tr("...")` literals into `:/ui_strings.txt`. Settings →
  "Create translation file" writes them as a template, keeping existing translations when
  updating a file.
- Rules for translatable text: pass `qsTr()` / `tr()` exactly one string literal (no `"a" + "b"`
  concatenation, or the extractor sees only the first part). Use `.arg()` for variable parts.
- `SettingsController.language` installs the translator, then emits `translationApplied`.
  `main.cpp` connects that to `QQmlEngine::retranslate()`, so the switch is live. Texts already
  computed in C++ (e.g. download notes) update the next time they are set.
- moc pitfall: a `#...` line inside a C++ raw string confuses moc's preprocessor, which then writes
  an empty `.moc`. Build test inputs from normal string literals instead.

## UI theme

- `qml/Theme.qml` is a QML singleton (`QT_QML_SINGLETON_TYPE` in CMake). It holds the accent,
  background, card color, radius and `uiScale`, persisted with QtCore `Settings` (category `theme`).
  Use `Theme.*` instead of literal colors; `Card.qml` and `Main.qml` read from it. Defaults: Black background,
  Red accent.
- Material's highlighted `Button` always draws white text. On light accents, use `AccentButton.qml`.
- Menus and drop-downs use `PopupBackground.qml` (lighter card shade, thin outline) and 30 px rows, not
  Material's large sheet: `CompactMenu.qml` + `CompactMenuItem.qml` (track and playlist right-click
  menus) and `CompactComboBox.qml` (list opens under the box).
- A nested layout inherits `fillWidth` from its children. Fixed-width side columns need
  `Layout.fillWidth: false` + `maximumWidth`.
- Mouse wheel: `VolumeControl` changes volume by ±5 % and `SeekBar` seeks by ±5 s (no modifier).
  Ctrl+wheel stays the UI zoom.

## Library and settings

- `SettingsPage.qml`: every section is a `SettingsPanel` card on the window background (no outer card), with
  the page title in its own card and the signature right under the last one. Its `EdgeFade`s use
  `baseColor: Theme.window` (EdgeFade's fade color, default `Theme.card`).
- `SettingsController` (QSettings): `library/folder` (default: the user's Music folder) and
  `window/minimizeToTray`. The Settings page also holds the slskd URL and API key.
- Startup: `core/LibraryIndex` keeps the last scan in `%LOCALAPPDATA%/grimPlayer/grimPlayer/cache/library-index.bin`
  (QDataStream, magic + version + folder; bump `kVersion` when Track changes). The library is shown from it at
  once, then an incremental scan re-reads only new files or ones whose size/mtime changed, and resets the models
  only if something changed. The refresh button and a folder change do a full scan. Tested in
  `tests/tst_libraryindex.cpp`. Pages other than Library are `LazyPage` loaders in `Main.qml`, built on first
  visit and then kept.
- `LibraryController` scans the library folder on a `QtConcurrent` worker (TagLib per file) at startup,
  when the folder changes, and from the refresh button next to "Library" (no file watching). Finished
  Soulseek downloads are added one by one (`addFile`). `QtConcurrent::run` cannot be cancelled, so a rescan requested
  mid-scan is queued. `addFile()` inserts one track (used for imported downloads).
- Cover art: `image://cover/<percent-encoded path>` from `CoverImageProvider` (async). It returns the
  embedded picture (TagLib `PICTURE`, front cover preferred) or cover/folder/front.jpg|png.
- `src/core/CoverCache` keeps scaled covers in memory (LRU; 32 MB for `CoverImageProvider`, since Qt Quick
  keeps its own copy of covers on screen) and as JPEG thumbnails in
  `%LOCALAPPDATA%/grimPlayer/grimPlayer/cache/covers`, keyed by path + mtime + size. An empty `.none`
  file remembers that a file has no cover. Decoding uses `QImageReader::setScaledSize`, so JPEGs decode at
  thumbnail size. It is safe to delete the folder. Tested in `tests/tst_covercache.cpp`.
  `CoverArt.qml` requests covers at 1.5× their on-screen pixel size (`width × Theme.uiScale × Screen.devicePixelRatio`;
  exactly 1× looked soft), not a fixed 2× of the logical size.
  Settings → Cache shows the size of `cache/covers` + `cache/artists` + `cache/lyrics` and "Clear cache" deletes their files
  (`SettingsController.clearCache()`; folders kept, in-memory covers and lyrics stay until restart).
- `LibraryPage.qml`: Albums are always a grid and Tracks always a list (choice remembered with QtCore
  `Settings`, category `library`). `AlbumsModel` derives albums from `LibraryModel` (name + artist; untagged
  files group by folder) and rebuilds on every library change. Clicking a track queues the whole
  list or album.
- Search: `LibraryController.filter` filters both models, 150 ms after the last keystroke.
  An empty filter applies at once (`openAlbumOf()` clears the search, then looks the album up).
  In the Tracks list the album name is a link to its album page (`openAlbumOf(url, true)` flashes the row).
  `AlbumsModel` regroups tracks only on `LibraryModel::tracksChanged` (scan/import); filter and sort only re-filter the
  grouped list. Counts come from each model's `count` property (an `Instantiator` per row was slow).
  The grid and track list use `reuseItems` so fast scrolling recycles delegates.
  Tracks match on title, artist or album;
  albums match on title or artist, or when any of their tracks matches. `LibraryModel::tracks()`
  always returns all tracks, and rows are the filtered subset.
- Album grid tiles keep one size (`kTile` 200 incl. padding); a wider panel spreads the spare width as even
  gaps (`sideGap` insets) instead of growing covers. The "Library" title shifts with the first cover.
- Views bind `model:` to the view-mode settings, never to `visible`. `visible` reads as false while the
  album page covers them, which would reset the model and lose the scroll position.
- Track order inside an album always comes from the tags: disc, then track number, then file
  name (`albumOrderLess` in `models/Track.h`; disc from the `DISCNUMBER` property). The album
  sort never reorders tracks within an album.
- Library extras: Shuffle play (`AlbumsModel.allUrls()` or `LibraryModel.urls()`, whatever is
  shown), an `EdgeFade` over the lists (bottom, and a short one at the top once scrolled; hidden at that end of the list), `SlimScrollBar.qml` for every list (shown only while
  scrolling or hovered), and the playing track's title in green (`sameFile()` compares URLs decoded).
- Genre sort in the Albums view shows a list of genres first (`AlbumsModel.genres`: {key, name, count} of the
  albums matching search/artist filters; untagged = "Unknown genre"). Clicking one sets
  `LibraryController.genreFilter` (a key) and the grid shows those albums with a "Genre ✕" chip; leaving genre
  sort clears it.
- Album sort (`AlbumsModel.sortOrder`, remembered as `library/albumSort`): artist, title, year (unknown
  last), rating (average of rated tracks, unrated last), genre (first non-empty track genre, empty
  last). Ties fall back to artist/title. A rating change rebuilds the albums only in rating sort.
  The open album page re-finds its album by first track after any rebuild.
  Tracks have the same orders (`LibraryModel.sortOrder`, `library/trackSort`): only the filtered rows are
  reordered; `tracks()` stays in artist order because `AlbumsModel` and `findTrack` index into it.
- Narrow windows (e.g. snapped to half the screen): the page column has `Layout.minimumWidth: 0`,
  the right column scales between 240 and 300 px, and wide header controls use fillWidth with
  min/max widths. Without that, the pages slid under the right column.
- Bottom row mirrors the columns: `SeekBar` sits under the pages and `PlaybackControls` under the right
  column, both 64 px high. Right column top to bottom: now playing, `RatingCard`, volume, controls.
- `FastWheelScroll.qml` (inside a Flickable) gives 240 px per wheel notch with a short animation.
- `PlayerController.url` + `AlbumsModel.rowForTrack()` let the now-playing card open the playing album; its
  artist name opens that artist's albums (`showArtist`, same as Statistics → Top artists).
- Album page rows: the playing track's number is replaced by a pause icon (play icon while paused).
- The album page has no back arrow: Escape or the mouse's back button returns to the grid/list.
- Statistics page: three highlight cards on top (`TopHighlight.qml`, from `StatsController.topTrack/
  topArtist/topAlbum`), then the totals, then the ranked lists. The top artist's photo comes from
  `src/core/ArtistImageFetcher`: Deezer `GET /search/artist?q=` (no key; exact name with the most fans preferred, generic
  "artist//" placeholder = none), `picture_big` saved in `%LOCALAPPDATA%/grimPlayer/grimPlayer/cache/artists`
  (`<sha1 of name>.jpg`, `.none` retried after 7 days; failures not cached). Looked up once per top-artist
  change. Tested in `tests/tst_artistimagefetcher.cpp`.
- Statistics rows are links: top track → its album page with that row flashing, top album → album
  page, top artist → library albums filtered to that artist (`LibraryController.artistFilter`,
  shown as "Albums by X ✕"). Album stats carry a `samplePath` to find the album.
- QML compares file URLs decoded and lower-cased (`decodeURIComponent(u.toString())`). The same
  URL can arrive percent-encoded from one model and decoded from another.
- App icon: a white star, `qml/icons/app.svg` (window + tray) and `resources/app.ico` (exe, via `resources/app.rc`;
  regenerate with `powershell -File resources/make_icon.ps1`).
- The tray menu starts with the playing song ("▶ Title – Artist", ❚❚ when paused; click opens the window), and
  the tray tooltip names it too.
- `TrayController` (needs `QApplication` + Qt Widgets for the menu): with minimize-to-tray on,
  `windowStateChanged(Minimized)` hides the window through a queued call. A synchronous hide during
  the minimize is undone by Windows.

## Download conversion

- `src/core/AudioConverter` converts each finished slskd download (state contains `Succeeded`)
  next to the source, using `ffmpeg` from PATH (`-map 0:a:0 -map_metadata -1`), to the format picked in the
  Soulseek page's filter row, after a divider (`SlskdController.downloadFormat`, `slskd/downloadFormat`): `m4a` (AAC 320 kbps,
  default), `mp3` (libmp3lame 320 kbps) or `flac`, or `original` = no conversion and no ffmpeg needed.
  A download already in the chosen format is moved, not re-encoded. Each queued job keeps the format it was
  queued with.
  It resamples to 48 kHz above 48 kHz, then copies all tags and pictures with TagLib
  (`setProperties` + `PICTURE` complex property), so freeform tags like ReplayGain survive.
- Output goes to `<name>.converting.<ext>` (the library scan skips those) and is renamed only after the tags
  are copied. The destructor kills ffmpeg and deletes the partial file. An existing target is never re-encoded.
- After a successful conversion `SlskdController` sends the original to the Recycle Bin
  (`QFile::moveToTrash`, hard delete only if that fails). `LibraryFiles::moveIntoLibrary` then moves
  the `.m4a` into the library folder, keeping its album subfolder, never overwriting (" (2)"), and
  removing the emptied download folder. `trackImported` adds it to the library. Downloads that are
  already `.m4a` skip conversion and are only moved.
- slskd's API does not report local paths. `SlskdController` reads `directories.downloads` from
  `GET /api/v0/options`, then finds the file by name (newest match) under that folder.
- `tests/tst_audioconverter.cpp` runs real ffmpeg and skips if ffmpeg is missing.

## Window chrome

- Single instance: `main.cpp` first tries `QLocalSocket` to `grimPlayer-<user name>`. If grim. already runs, it
  sends its file argument (may be empty), calls `AllowSetForegroundWindow` (Windows) and exits before touching any
  data; the running one (`QLocalServer`) opens that file, if any, and `TrayController::restoreWindow()` brings the
  window back from the tray/taskbar (un-minimizes, keeps it maximized) to the front.
- The window is frameless (`Qt.FramelessWindowHint`). `main.cpp` asks DWM for Windows 11 rounded corners
  (`DWMWA_WINDOW_CORNER_PREFERENCE`, links `dwmapi`). `qml/components/TitleBar.qml` handles drag
  (`startSystemMove`), double-click maximize, the window buttons (Segoe MDL2 Assets glyphs) and the
  slskd status dot; clicking the dot checks slskd again.
  `ResizeEdge.qml` rebuilds the resize border with `startSystemResize`.
- The title bar sits outside the Ctrl+wheel zoom area, so its buttons keep a fixed size.
- MSVC: `/wd4702` on the app target only. qmlcachegen output triggers C4702 inside Qt headers.

## Linux

- Built and packed on GitHub Actions (`.github/workflows/linux-appimage.yml`, repo `beeprat/grim`, private): Ubuntu
  22.04, Qt 6.10.3 (install-qt-action), g++-12, a static audio-only FFmpeg 7.1.2 built in CI and cached (mov/aac
  demuxers, aac/alac decoders, swresample; Ubuntu's shared FFmpeg doubled the AppImage to 95 MB, now ~50 MB), all tests with `QT_QPA_PLATFORM=offscreen`, then
  linuxdeploy + its Qt plugin into `grim-x86_64.AppImage` (run artifact). Nothing Linux-specific compiles on Windows.
- Platform splits: `audio/AacSource.h` picks `MfAudioSource` (Windows) or `FfmpegAudioSource` (libavformat/libavcodec/
  swresample, FFmpeg 4 and 5+ APIs both handled); `controllers/Mpris` (MPRIS 2 on the session bus) replaces `MediaKeys`;
  `DiscordIpc` finds the Unix socket in `$XDG_RUNTIME_DIR` (plain, Flatpak or Snap folder); "Open containing folder"
  uses D-Bus `org.freedesktop.FileManager1.ShowItems`; `TitleBar.qml` draws the window button glyphs where Segoe MDL2
  Assets is missing; `main.cpp` loads Selawik (`resources/fonts`, SIL OFL, Segoe UI metrics) as the app font.
- File paths for TagLib/miniaudio/FFmpeg go through `QFileInfo::filesystemFilePath()` (wchar_t on Windows, UTF-8 on
  Linux), never `toStdWString()`.
- linuxdeploy-plugin-qt packs only `platforms/libqwayland.so`; the workflow copies Qt's `plugins/wayland-*` folders
  (EGL buffer, decorations, shell) and deploys their deps, or Qt Quick aborts on Wayland. Tested working on Linux Mint
  (X11) and CachyOS (Wayland), 2026-10-18.
- MPRIS sends the cover as `mpris:artUrl`: a 512 px JPEG from `CoverCache::thumbnailFile()` in the covers cache.
- Not done: rounded window corners on Linux (the frameless window is square there).

## Planned dependencies (not yet integrated)

libebur128 (ReplayGain scan). Add it with `find_package` / `FetchContent` when its feature lands.

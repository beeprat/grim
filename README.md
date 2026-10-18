# grim.

A fast, minimal music player for Windows, for people who keep their music as files.

grim. plays your local library (MP3, FLAC, M4A/AAC, WAV) with a dark, card-based interface, synced lyrics, ratings, playlists and listening statistics. It can also find and download music through Soulseek (via [slskd](https://github.com/slskd/slskd)), scrobble to Last.fm and show what you are listening to on Discord.

Native C++20 and Qt 6 (QML). No Electron, no web view, no account.

<!-- Screenshot: add one here, e.g. ![grim.](docs/screenshot.png) -->

## Features

**Library**
- Albums as a cover grid or all tracks as a list, with search by title, artist or album
- Sort by artist, title, year, average rating or genre; genre sort lists your genres first
- Album pages with track numbers, total rating and a play/pause indicator on the playing track
- Click an album or artist name anywhere to jump to it
- Starts instantly: the library is remembered between runs and only new or changed files are re-read
- "Delete album" moves an album's files to the Recycle Bin, after asking

**Playback**
- Gapless playback, or a crossfade from 0.1 to 12 seconds
- Effects: slowed (vinyl-style), reverb and pitch shift (±12 semitones, tempo kept)
- Remembers the last song and position when you quit
- Media keys, system tray with the current song, Windows 11 rounded window

**Lyrics**
- Synced lyrics that follow the song; click a line to jump there
- Read from the file's tags or a `.lrc` file, or looked up online on [LRCLIB](https://lrclib.net) (can be turned off)

**Ratings, favorites and playlists**
- Five-star ratings and favorites per track
- Playlists, with a resizable sidebar that collapses to icons

**Statistics**
- Listening time and play counts (a play counts after 30 seconds)
- Top tracks, artists and albums, with artist photos from Deezer

**Soulseek downloads** (optional, needs slskd)
- Search by album, artist or track, filter by format and bitrate
- Downloads go straight into your library, converted to M4A, MP3 or FLAC, or kept as downloaded

**Online services** (optional, off until you set them up)
- Last.fm scrobbling with your own API key
- Discord Rich Presence with album covers

**Other**
- Accent colors, three dark backgrounds, corner roundness and interface zoom (Ctrl + mouse wheel)
- Translatable: create a translation file from Settings and fill it in

## Requirements

- Windows 10 or 11, 64-bit
- Optional: [slskd](https://github.com/slskd/slskd) for Soulseek downloads
- Optional: [ffmpeg](https://ffmpeg.org) on PATH to convert downloads (not needed with "Save as original")

## Install

Download the latest release and either:
- run `grim-setup.exe`, or
- extract `GRIM-win64.7z` anywhere and run `GRIM\GRIM.exe` (portable, nothing to install).

Settings, ratings, playlists and statistics are stored in `%APPDATA%\grimPlayer\grimPlayer` (preferences in the registry under `HKEY_CURRENT_USER\Software\grimPlayer`), caches in `%LOCALAPPDATA%\grimPlayer\grimPlayer`. Uninstalling keeps them.

## Setting up the optional services

**Soulseek:** run slskd and log it in to Soulseek. In grim., open Settings → slskd and enter its address (default `http://localhost:5030`) and API key. If slskd runs with `--no-auth`, also start it with `--http-ip-address 127.0.0.1` so only your PC can reach it. The status dot in the title bar shows whether slskd is reachable; click it to check again.

**Last.fm:** create an API account at [last.fm/api/account/create](https://www.last.fm/api/account/create), put its API key and shared secret in `%APPDATA%\grimPlayer\grimPlayer\lastfm.txt` (`api_key = ...`, `shared_secret = ...`), then press "Connect account" in Settings → Last.fm.

**Discord:** create an application at [discord.com/developers/applications](https://discord.com/developers/applications) (its name is what Discord shows, e.g. "grim.") and put its Application ID in `%APPDATA%\grimPlayer\grimPlayer\discord.txt` (`application_id = ...`). For album covers, also register an anonymous app at [api.imgur.com/oauth2/addclient](https://api.imgur.com/oauth2/addclient) and put its Client-ID there (`imgur_client_id = ...`). Then turn on Settings → Discord. Covers are uploaded to imgur anonymously; anyone with the link can see them.

## Privacy

grim. has no account and sends nothing by itself. These features talk to other services, only when used:

| Feature | Service | What is sent |
|---|---|---|
| Online lyrics (on by default, can be turned off) | lrclib.net | artist, title, album, length |
| Top artist photo | deezer.com | artist name |
| Last.fm (opt-in) | last.fm | the songs you play |
| Discord (opt-in) | Discord app on your PC, imgur.com | song, artist, album, cover image |
| Soulseek (opt-in) | your slskd | your searches and downloads |

## Building from source

Needs Visual Studio 2022 Build Tools (MSVC), Qt 6.8 or newer (MSVC 64-bit kit), CMake 3.22+ and Ninja. miniaudio, TagLib and SoundTouch are downloaded by CMake.

```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.10.3\msvc2022_64"   # your Qt kit
cmake --preset release
cmake --build --preset release
ctest --test-dir build/release
.\build\release\GRIM.exe
```

Run these from an "x64 Native Tools Command Prompt for VS 2022", or call `vcvars64.bat` first. The build copies the Qt runtime next to the exe.

## Credits

Built with:
- [Qt 6](https://www.qt.io) (LGPL-3.0)
- [miniaudio](https://miniaud.io) (public domain / MIT-0)
- [TagLib](https://taglib.org) (LGPL-2.1 / MPL-1.1)
- [SoundTouch](https://www.surina.net/soundtouch) (LGPL-2.1)
- Reverb after Jezar's Freeverb (public domain)
- Icons: [Material Design Icons](https://fonts.google.com/icons) (Apache-2.0)

Lyrics from [LRCLIB](https://lrclib.net), artist photos from [Deezer](https://www.deezer.com).

## License

<!-- Pick a license and add a LICENSE file, e.g. GPL-3.0 or MIT. -->

grim. - šarić, 2026.

grim. pušta tvoj lokalni library. (MP3, FLAC, M4A/AAC, WAV)
podržava sinhronizovani tekst, ocenjivanje, plejliste i statistike slušanja.
takođe može da pretražuje i preuzima muziku sa Soulseek (via [slskd](https://github.com/slskd/slskd)), scrobble na Last.fm i prikazuje šta slušate na Discord.

Native C++20 i Qt 6 (QML). bez Electron, bez web view, bez naloga.

## <img width="1920" height="1050" alt="screenshot 1" src="https://github.com/user-attachments/assets/b0a7c2ea-5422-45e2-991a-be71b95c7740" />
## <img width="1920" height="1050" alt="screenshot 2" src="https://github.com/user-attachments/assets/b73febdc-cccc-43bd-8220-3550d629800d" />
## <img width="1920" height="1050" alt="screenshot 3" src="https://github.com/user-attachments/assets/c5cf9b77-9d3f-431a-bc7e-8ab727b132cb" />

## features:
- soulseek preuzimanja + konvertovanje (potreban ffmpeg)
- statistike sa top trakama, albumima i izvođačima + export/import.
- sinhronizovani tekstovi. (čitanje sa tagova fajla, `.lrc` fajla ili preko [LRCLIB](https://lrclib.net))
- ocenjivanje pesama = prosečna ocena albuma.
- a-b looping.
- in-program ažuriranje. (windows)
- customization. (ograničeni)
- mala download veličina.
- speed i reverb slideri.
- gapless playback.
- crossfade.
- last.fm scrobbling.
- discord rich presence. 
- translation support.

## requirements:
- windows: 10 ili 11, 64-bit.
- linux: 64-bit x86 distro sa glibc 2.35 ili novije.
- ophodno: Soulseek nalog (grim. sam preuzima [slskd](https://github.com/slskd/slskd)).
- ophodno: [ffmpeg](https://ffmpeg.org) na PATH za konvertovanje. (nije potrebno kad je "Save as original")

## instalacija:
preuzmi najnoviji release:
- pokreni `grim-setup-x64.exe`, ili
- ekstraktuj `GRIM-win64.7z` bilo gde i pokreni `GRIM.exe`. (portabilna instalacija, windows)
- pokreni `grim-x86_64.AppImage`. (linux)

podešavanja, ocene, plejliste i statistike su smeštene u `%APPDATA%\grimPlayer\grimPlayer`, cache u`%LOCALAPPDATA%\grimPlayer\grimPlayer`.
brisanje programa ne briše ove podatke.

## podešavanje slskd:

**soulseek:** u podešavanjima → soulseek upisati soulseek username i password. grim. sam preuzima [slskd](https://github.com/slskd/slskd) (oko 55 MB) i pokreće ga samo dok je grim. otvoren.

statusna tačka u title baru pokazuje da li je slskd dostupan.

## privatnost:

grim. ne uzima nikakve lične podatke.
sledeće funkcije komuniciraju sa drugim uslugama po potrebi:

| funkcija| usluga| šta se šalje |
|---|---|---|
| online tekst (uključeno po default, može se isključiti) | lrclib.net | izvođač, ime, album, dužina |
| slika za top izvođača | deezer.com | ime izvođača |
| Last.fm (opt-in) | last.fm | pesme koje slušaš |
| Discord (opt-in) | Discord program na računaru, imgur.com | pesma, izvođač, album, album cover|
| Soulseek (opt-in) | tvoj slskd | tvoje pretrage i preuzimanja |

## Credits:

napravljeno uz:
- [Qt 6](https://www.qt.io) (LGPL-3.0)
- [miniaudio](https://miniaud.io) (public domain / MIT-0)
- [TagLib](https://taglib.org) (LGPL-2.1 / MPL-1.1)
- reverb od Jezar Freeverb (public domain)
- ikonice od [Material Design Icons](https://fonts.google.com/icons) (Apache-2.0)

tekstovi od [LRCLIB](https://lrclib.net), slike izvođača sa [Deezer](https://www.deezer.com).

grim. - GRIMSIXSTAR / beeprat, 2026.

brz, minimalističan music player za Windows, za ljude koji čuvaju muziku lokalno.

grim. pušta tvoj lokalni library. (MP3, FLAC, M4A/AAC, WAV)
podržava sinhronizovani tekst, ocenjivanje, plejliste i statistike slušanja.
takođe može da pretražuje i preuzima muziku sa Soulseek (via [slskd](https://github.com/slskd/slskd)), scrobble na Last.fm i prikazuje šta slušate na Discord.

Native C++20 i Qt 6 (QML). bez Electron, bez web view, bez naloga.

<img width="1198" height="758" alt="example" src="https://github.com/user-attachments/assets/0bde48ab-56dd-49fa-8b83-12ebec089585" />

## funkcije

**library:**
- albumi prikazani kao grid ili sve trake kao lista, uz pretragu po nazivu, izvođaču ili albumu.
- sortiranje po izvođaču, imenu, godini, prosečnoj oceni ili žanru.
- stranica albuma sa brojevima numera, ukupnom ocenom i play/pause indikator na pesmi koja se trenutno pušta.
- optimizovano pokretanje
- "Delete album" premešta sadržaj u Recycle Bin, nakon potvrde.

**playback:**
- gapless playback, ili crossfade od 0.1 do 12 sekundi.
- efekti: slowed (vinyl-stil), reverb i pitch shift. (±12 semitona, tempo je zadržan)
- pamti poslednju pesmu i dužinu prilikom izlaska iz playera.
- media keys, system tray sa trenutnom pesmom, Windows 11 rounded window.

**tekst:**
- sinhronizovani tekstovi koji prate pesmu.
- čitanje sa tagova fajla, `.lrc` fajla, ili preko [LRCLIB](https://lrclib.net)

**ocene, favoriti i plejliste:**
- ocenjivanje od jedne do pet zvezdica i favorit trake.
- plejliste, sa resizable sidebar.

**statistike:**
- listening time i play count (play se računa nakon 30 sekundi)
- top trake, izvođači i albumi, sa slikama izvođača sa Deezer-a.

*soulseek preuzimanje:* (ophodno, **potreban slskd**)
- pretraga po albumu, izvođaču ili traci, filtriranje po formatu i bitrate.
- preuzimanja idu direktno u library, konvertovanje u M4A, MP3 ili FLAC, ili original.

**online usluge:** (ophodne)
- last.fm scrobbling
- discord rich presence

**ostalo:**
- accent boje, tri tamne pozadine, corner roundness i interfejs zum (ctrl + točkić)
- podrška za prevod: fajl za prevod u podešavanjima. (trenutno samo Srpski i Engleski)
- slowed, reverb i pitch slideri.
- minimize na system tray
- clear cache dugme

## requirements

- windows 10 ili 11, 64-bit
- ophodno: [slskd](https://github.com/slskd/slskd) za Soulseek preuzimanja.
- ophodno: [ffmpeg](https://ffmpeg.org) na PATH za konvertovanje. (nije potrebno kad je "Save as original")

## instalacija

preuzmi najnoviji release:
- pokreni `grim-setup-x64.exe`, ili
- ekstraktuj `GRIM-win64.7z` bilo gde i pokreni `GRIM.exe`. (portabilna instalacija)

podešavanja, ocene, plejliste i statistike su smeštene u `%APPDATA%\grimPlayer\grimPlayer`, cache u`%LOCALAPPDATA%\grimPlayer\grimPlayer`.
brisanje aplikacije ne briše ove podatke.

## podešavanje slskd

**Soulseek:** pokrenuti slskd i ulogovati se u Soulseek, u grim., otvoriti podešavanja → slskd i ukucati adresu (podrazumevana: `http://localhost:5030`) i API key.
ako slskd radi sa `--no-auth`, takođe ga započeti sa `--http-ip-address 127.0.0.1` da bi ga samo tvoj računar pristupio.
statusna tačka u title baru pokazuje da li je slskd dostupan.

## privatnost

grim. ne uzima nikakve lične podatke.
sledeće funkcije komuniciraju sa drugim uslugama po potrebi:

| funkcija| usluga| šta se šalje |
|---|---|---|
| online tekst (uključeno po default, može se isključiti) | lrclib.net | izvođač, ime, album, dužina |
| slika za top izvođača | deezer.com | ime izvođača |
| Last.fm (opt-in) | last.fm | pesme koje slušaš |
| Discord (opt-in) | Discord program na računaru, imgur.com | pesma, izvođač, album, album cover|
| Soulseek (opt-in) | tvoj slskd | tvoje pretrage i preuzimanja |

## Credits

napravljeno uz:
- [Qt 6](https://www.qt.io) (LGPL-3.0)
- [miniaudio](https://miniaud.io) (public domain / MIT-0)
- [TagLib](https://taglib.org) (LGPL-2.1 / MPL-1.1)
- [SoundTouch](https://www.surina.net/soundtouch) (LGPL-2.1)
- reverb od Jezar Freeverb (public domain)
- ikonice od [Material Design Icons](https://fonts.google.com/icons) (Apache-2.0)

tekstovi od [LRCLIB](https://lrclib.net), slike izvođača sa [Deezer](https://www.deezer.com).

grim. - GRIMSIXSTAR / šarić / beeprat, 2026.

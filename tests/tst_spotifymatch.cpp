#include "core/SpotifyMatch.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

Track track(const QString &path, const QString &title, const QString &artist, const QString &album, qint64 durationMs)
{
    Track t;
    t.path = path;
    t.title = title;
    t.artist = artist;
    t.album = album;
    t.durationMs = durationMs;
    return t;
}

} // namespace

class TestSpotifyMatch : public QObject
{
    Q_OBJECT

private slots:
    void linksParse()
    {
        const auto link = Spotify::parseLink(u"https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M?si=abc123"_s);
        QCOMPARE(link.kind, u"playlist"_s);
        QCOMPARE(link.id, u"37i9dQZF1DXcBWIGoYBM5M"_s);
        QCOMPARE(Spotify::parseLink(u"spotify:album:4LH4d3cOWNNsVw41Gqt2kv"_s).kind, u"album"_s);
        QCOMPARE(Spotify::parseLink(u"https://open.spotify.com/intl-de/album/4LH4d3cOWNNsVw41Gqt2kv"_s).id,
                 u"4LH4d3cOWNNsVw41Gqt2kv"_s);
        QVERIFY(Spotify::parseLink(u"https://example.com/track/1"_s).kind.isEmpty());
    }

    void embedPageParses()
    {
        const QByteArray json = R"({"props":{"pageProps":{"state":{"data":{"entity":{"type":"album","name":"Rumours",)"
                                R"("trackList":[{"title":"Dreams","subtitle":"Fleetwood Mac","duration":257800},)"
                                R"({"title":"Duet","subtitle":"A, B","duration":1000}]}}}}}})";
        const QByteArray html = "<html><script id=\"__NEXT_DATA__\" type=\"application/json\">" + json + "</script></html>";
        QString name;
        QList<Spotify::RemoteTrack> tracks;
        QVERIFY(Spotify::parseEmbedPage(html, &name, &tracks));
        QCOMPARE(name, u"Rumours"_s);
        QCOMPARE(tracks.size(), 2);
        QCOMPARE(tracks.at(0).album, u"Rumours"_s);
        QCOMPARE(tracks.at(0).durationMs, 257800);
        QCOMPARE(tracks.at(1).artists, QStringList({u"A"_s, u"B"_s}));
        QVERIFY(!Spotify::parseEmbedPage("<html></html>", &name, &tracks));
    }

    void titlesLoseTheirExtras()
    {
        QCOMPARE(Spotify::baseTitle(u"Dreams - 2004 Remaster"_s), u"dreams"_s);
        QCOMPARE(Spotify::baseTitle(u"Go Your Own Way (feat. Someone)"_s), u"go your own way"_s);
        QCOMPARE(Spotify::baseTitle(u"Señorita [Live]"_s), u"senorita"_s);
        QCOMPARE(Spotify::baseTitle(u"Rock & Roll"_s), u"rock and roll"_s);
        QCOMPARE(Spotify::baseTitle(u"(Untitled)"_s), u"untitled"_s); // nothing left: keep it
    }

    void artistTagsSplit()
    {
        const QStringList names = Spotify::artistNames(u"Simon & Garfunkel"_s);
        QVERIFY(names.contains(u"simon and garfunkel"_s));
        QVERIFY(names.contains(u"garfunkel"_s));
        QVERIFY(Spotify::artistNames(u"Daft Punk feat. Pharrell Williams"_s).contains(u"pharrell williams"_s));
        QVERIFY(Spotify::artistNames(u"The Beatles"_s).contains(u"beatles"_s));
    }

    void matchesAcrossTagDifferences()
    {
        const QList<Track> library = {
            track(u"/m/rumours/dreams.flac"_s, u"Dreams"_s, u"Fleetwood Mac"_s, u"Rumours"_s, 257000),
            track(u"/m/live/dreams.flac"_s, u"Dreams (Live)"_s, u"Fleetwood Mac"_s, u"Live"_s, 290000),
            track(u"/m/ram/lucky.flac"_s, u"Get Lucky"_s, u"Daft Punk feat. Pharrell Williams"_s, u"RAM"_s, 369000),
            track(u"/m/beatles/help.flac"_s, u"Help!"_s, u"The Beatles"_s, u"Help!"_s, 138000),
        };
        const Spotify::Matcher matcher(library);

        // Closest length wins between the studio and live file.
        QCOMPARE(matcher.match({u"Dreams - 2004 Remaster"_s, {u"Fleetwood Mac"_s}, u"Rumours"_s, 257800}),
                 u"/m/rumours/dreams.flac"_s);
        QCOMPARE(matcher.match({u"Dreams - Live"_s, {u"Fleetwood Mac"_s}, u"Live"_s, 290100}),
                 u"/m/live/dreams.flac"_s);
        // Any of the Spotify artists may match a name in the local tag.
        QCOMPARE(matcher.match({u"Get Lucky (feat. Pharrell Williams and Nile Rodgers)"_s,
                                {u"Daft Punk"_s, u"Pharrell Williams"_s, u"Nile Rodgers"_s}, u"Random Access Memories"_s, 369000}),
                 u"/m/ram/lucky.flac"_s);
        QCOMPARE(matcher.match({u"Help! - Remastered 2009"_s, {u"The Beatles"_s}, u"Help!"_s, 139000}),
                 u"/m/beatles/help.flac"_s);
        QCOMPARE(matcher.match({u"Yesterday"_s, {u"The Beatles"_s}, u"Help!"_s, 125000}), QString());
        QCOMPARE(matcher.match({u"Dreams"_s, {u"The Cranberries"_s}, {}, 271000}), QString());
    }
};

QTEST_GUILESS_MAIN(TestSpotifyMatch)
#include "tst_spotifymatch.moc"

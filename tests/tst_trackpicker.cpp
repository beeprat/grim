#include "core/TrackPicker.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

SlskdSearchFile file(const QString &path, const QString &extension, int lengthSec, bool freeSlot = true, int bitRate = 0)
{
    SlskdSearchFile f;
    f.username = u"user"_s;
    f.filename = path;
    f.extension = extension;
    f.lengthSec = lengthSec;
    f.hasFreeSlot = freeSlot;
    f.bitRate = bitRate;
    return f;
}

} // namespace

class TestTrackPicker : public QObject
{
    Q_OBJECT

private slots:
    void searchTextDropsExtras()
    {
        QCOMPARE(TrackPicker::searchText({u"Second Hand News - 2004 Remaster"_s, {u"Fleetwood Mac"_s}, {}, 0}),
                 u"fleetwood mac second hand news"_s);
        QCOMPARE(TrackPicker::searchText({u"Mrs. Robinson"_s, {u"Simon & Garfunkel"_s}, {}, 0}),
                 u"simon garfunkel mrs robinson"_s);
        QCOMPARE(TrackPicker::searchText({u"I Don't Want to Know"_s, {u"Fleetwood Mac"_s}, {}, 0}),
                 u"fleetwood mac i want to know"_s);
    }

    void fallbackSearches()
    {
        QCOMPARE(TrackPicker::searchTexts({u"My Own Summer (Shove It)"_s, {u"Deftones"_s}, u"Around the Fur"_s, 0}),
                 QStringList({u"deftones my own summer"_s, u"*eftones my own summer"_s, u"my own summer around the fur"_s}));
        QCOMPARE(TrackPicker::albumSearchTexts(u"Deftones"_s, u"Around the Fur"_s),
                 QStringList({u"deftones around the fur"_s, u"*eftones around the fur"_s, u"around the fur"_s}));
        // No album: the last fallback is the title alone.
        QCOMPARE(TrackPicker::searchTexts({u"Help!"_s, {u"The Beatles"_s}, {}, 0}).last(), u"help"_s);
    }

    void ranksTheRightFileFirst()
    {
        const Spotify::RemoteTrack song{u"Songbird - 2004 Remaster"_s, {u"Fleetwood Mac"_s}, u"Rumours"_s, 200000};
        const QList<SlskdSearchFile> files = {
            file(u"@@a\\Music\\Fleetwood Mac\\Rumours\\09 Songbird (Live).flac"_s, u"flac"_s, 200),   // 0 live
            file(u"@@b\\Music\\Fleetwood Mac\\Rumours\\09 Songbird.mp3"_s, u"mp3"_s, 200, true, 320), // 1
            file(u"@@c\\Music\\Fleetwood Mac\\Rumours\\09 Songbird.flac"_s, u"flac"_s, 201),          // 2 best
            file(u"@@d\\Music\\Christine McVie\\Songbird.flac"_s, u"flac"_s, 200),                    // 3 other artist
            file(u"@@e\\Music\\Fleetwood Mac\\Rumours\\09 Songbird.flac"_s, u"flac"_s, 260),          // 4 wrong length
            file(u"@@f\\Music\\Fleetwood Mac\\Rumours\\cover.jpg"_s, u"jpg"_s, 0),                    // 5 not audio
            file(u"@@g\\Music\\Fleetwood Mac\\Rumours\\09 Songbird.mp3"_s, u"mp3"_s, 200, true, 128), // 6 low bitrate
            file(u"@@h\\Music\\Fleetwood Mac\\Rumours\\09 Songbird.flac"_s, u"flac"_s, 200, false),   // 7 no free slot
        };
        QCOMPARE(TrackPicker::rank(files, song, u"flac"_s), QList<int>({2, 7, 1}));
        // MP3 wanted: it comes first, FLACs still after it.
        QCOMPARE(TrackPicker::rank(files, song, u"mp3"_s).first(), 1);
    }

    void liveSongsMayBeLive()
    {
        const Spotify::RemoteTrack song{u"Dreams (Live)"_s, {u"Fleetwood Mac"_s}, {}, 0};
        const QList<SlskdSearchFile> files = {file(u"@@a\\Fleetwood Mac\\Live\\Dreams (Live).flac"_s, u"flac"_s, 290)};
        QCOMPARE(TrackPicker::rank(files, song, {}), QList<int>({0}));
    }
};

QTEST_GUILESS_MAIN(TestTrackPicker)
#include "tst_trackpicker.moc"

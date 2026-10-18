#include "FakeHttpServer.h"
#include "core/SlskdClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <optional>

using namespace Qt::StringLiterals;

Q_DECLARE_METATYPE(SlskdStatus)

// The fake server stands in for slskd.
using FakeSlskd = FakeHttpServer;

class TestSlskdClient : public QObject
{
    Q_OBJECT

private slots:
    void searchPostsQueryWithApiKey()
    {
        FakeSlskd fake;
        fake.responseBody = R"({"id":"search-1","state":"InProgress"})";
        SlskdClient client;
        client.setServer(fake.url(), u"secret"_s);

        QString searchId;
        client.startSearch(u"aphex twin"_s, [&](const QString &id) { searchId = id; });
        QTRY_COMPARE(searchId, u"search-1"_s);

        QCOMPARE(fake.requests.size(), 1);
        const FakeSlskd::Request &request = fake.requests.first();
        QCOMPARE(request.method, "POST");
        QCOMPARE(request.path, "/api/v0/searches");
        QCOMPARE(request.apiKey, "secret");
        QCOMPARE(QJsonDocument::fromJson(request.body).object().value("searchText"_L1).toString(), u"aphex twin"_s);
    }

    void downloadEncodesUsernameAndSendsFile()
    {
        FakeSlskd fake;
        SlskdClient client;
        client.setServer(QUrl(fake.url().toString() + u"/"_s), u"secret"_s); // trailing slash must not double up

        SlskdSearchFile file;
        file.username = u"dj shadow/x"_s;
        file.filename = u"@@music\\Endtroducing\\01 Best Foot Forward.flac"_s;
        file.size = 123456;

        SlskdSearchFile second = file;
        second.filename = u"@@music\\Endtroducing\\02 Building Steam.flac"_s;
        second.size = 42;

        bool queued = false;
        client.enqueueDownloads({file, second}, [&] { queued = true; });
        QTRY_VERIFY(queued);

        QCOMPARE(fake.requests.size(), 1); // a whole album is one request
        const FakeSlskd::Request &request = fake.requests.first();
        QCOMPARE(request.method, "POST");
        QCOMPARE(request.path, "/api/v0/transfers/downloads/dj%20shadow%2Fx");
        const QJsonArray sent = QJsonDocument::fromJson(request.body).array();
        QCOMPARE(sent.size(), 2);
        QCOMPARE(sent[0].toObject().value("filename"_L1).toString(), file.filename);
        QCOMPARE(sent[0].toObject().value("size"_L1).toInteger(), 123456);
        QCOMPARE(sent[1].toObject().value("size"_L1).toInteger(), 42);
    }

    void rejectedKeyReportsErrorOnce()
    {
        FakeSlskd fake;
        fake.status = 401;
        SlskdClient client;
        client.setServer(fake.url(), u"wrong"_s);
        QSignalSpy errors(&client, &SlskdClient::errorOccurred);

        bool called = false;
        client.fetchDownloads([&](QList<SlskdDownload>) { called = true; });
        QTRY_COMPARE(errors.count(), 1);
        QVERIFY(!called);
        QVERIFY(errors.first().first().toString().contains(u"API key"_s));
    }

    void clearFinishedDownloadsSendsDelete()
    {
        FakeSlskd fake;
        fake.status = 204;
        fake.responseBody = "";
        SlskdClient client;
        client.setServer(fake.url(), QString());

        bool cleared = false;
        client.clearFinishedDownloads([&] { cleared = true; });
        QTRY_VERIFY(cleared);
        QCOMPARE(fake.requests.first().method, "DELETE");
        QCOMPARE(fake.requests.first().path, "/api/v0/transfers/downloads/all/completed");
    }

    void noApiKeySendsNoHeader()
    {
        FakeSlskd fake;
        fake.responseBody = "[]";
        SlskdClient client;
        client.setServer(fake.url(), QString()); // slskd started with --no-auth
        QVERIFY(client.isConfigured());

        bool called = false;
        client.fetchDownloads([&](QList<SlskdDownload>) { called = true; });
        QTRY_VERIFY(called);
        QVERIFY(fake.requests.first().apiKey.isNull());
    }

    void status_data()
    {
        QTest::addColumn<int>("httpStatus");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<SlskdStatus>("expected");
        QTest::newRow("logged in") << 200 << QByteArray(R"({"server":{"state":"Connected, LoggedIn","isLoggedIn":true}})")
                                   << SlskdStatus::Online;
        QTest::newRow("not logged in") << 200 << QByteArray(R"({"server":{"state":"Disconnected","isLoggedIn":false}})")
                                       << SlskdStatus::NotLoggedIn;
        QTest::newRow("auth on, no key") << 401 << QByteArray("{}") << SlskdStatus::AuthRequired;
    }

    void status()
    {
        QFETCH(int, httpStatus);
        QFETCH(QByteArray, body);
        QFETCH(SlskdStatus, expected);

        FakeSlskd fake;
        fake.status = httpStatus;
        fake.responseBody = body;
        SlskdClient client;
        client.setServer(fake.url(), QString());
        QSignalSpy errors(&client, &SlskdClient::errorOccurred);

        std::optional<SlskdStatus> result;
        client.fetchStatus([&](SlskdStatus status) { result = status; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(*result, expected);
        QCOMPARE(fake.requests.first().path, "/api/v0/application");
        QCOMPARE(errors.count(), 0); // status never goes through errorOccurred
    }

    void statusOfflineWhenNothingListens()
    {
        SlskdClient client;
        client.setServer(QUrl(u"http://127.0.0.1:1"_s), QString());
        std::optional<SlskdStatus> result;
        client.fetchStatus([&](SlskdStatus status) { result = status; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 10000);
        QCOMPARE(*result, SlskdStatus::Offline);
    }

    void unreachableServerReportsError()
    {
        SlskdClient client;
        client.setServer(QUrl(u"http://127.0.0.1:1"_s), u"key"_s);
        QSignalSpy errors(&client, &SlskdClient::errorOccurred);
        client.fetchDownloads([](QList<SlskdDownload>) {});
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 15000);
    }

    void parsesSearchResponses()
    {
        const QByteArray json = R"([
            {"username":"alice","hasFreeUploadSlot":true,"queueLength":0,"uploadSpeed":900000,
             "files":[
               {"filename":"@@a\\Album\\01 Song.flac","size":30000000,"extension":"FLAC","bitDepth":16,"sampleRate":44100,"length":245},
               {"filename":"@@a\\Album\\02 Song.mp3","size":9000000,"extension":"","bitRate":320,"length":200},
               {"filename":"@@a\\Album\\03 Hidden.mp3","size":1,"isLocked":true}
             ]},
            {"username":"","files":[{"filename":"x.mp3"}]},
            {"username":"bob","hasFreeUploadSlot":false,"queueLength":7,"files":[{"filename":"b\\c.ogg","size":5}]}
        ])";

        const QList<SlskdSearchFile> files = Slskd::parseSearchResponses(json);
        QCOMPARE(files.size(), 3);
        QCOMPARE(files[0].username, u"alice"_s);
        QCOMPARE(files[0].extension, u"flac"_s);
        QCOMPARE(files[0].bitDepth, 16);
        QCOMPARE(files[0].lengthSec, 245);
        QVERIFY(files[0].hasFreeSlot);
        QCOMPARE(files[1].extension, u"mp3"_s); // derived from the file name
        QCOMPARE(files[1].bitRate, 320);
        QCOMPARE(files[2].username, u"bob"_s);
        QCOMPARE(files[2].queueLength, 7);

        QVERIFY(Slskd::parseSearchResponses("not json").isEmpty());
    }

    void parsesDownloads()
    {
        const QByteArray json = R"([
            {"username":"alice","directories":[{"directory":"@@a\\Album","files":[
                {"id":"t1","filename":"@@a\\Album\\01 Song.flac","state":"InProgress","size":200,"bytesTransferred":50},
                {"id":"t2","filename":"@@a\\Album\\02 Song.flac","state":"Completed, Succeeded","size":100,"bytesTransferred":100}
            ]}]}
        ])";

        const QList<SlskdDownload> downloads = Slskd::parseDownloads(json);
        QCOMPARE(downloads.size(), 2);
        QCOMPARE(downloads[0].id, u"t1"_s);
        QCOMPARE(downloads[0].username, u"alice"_s);
        QCOMPARE(downloads[0].bytesTransferred, 50);
        QVERIFY(!Slskd::isTransferFinished(downloads[0].state));
        QVERIFY(Slskd::isTransferFinished(downloads[1].state));
    }

    void searchCompletion()
    {
        QVERIFY(!Slskd::isSearchComplete(u"InProgress"_s));
        QVERIFY(!Slskd::isSearchComplete(u"Requested"_s));
        QVERIFY(Slskd::isSearchComplete(u"Completed, ResponseLimitReached"_s));
        QVERIFY(Slskd::isSearchComplete(u"Completed, TimedOut"_s));
        QVERIFY(Slskd::isSearchComplete(u"Errored"_s));
    }

    void folderNames_data()
    {
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("album");
        QTest::addColumn<QString>("artist");
        QTest::newRow("artist folder") << u"@@x\\Music\\The Doors\\1967 - Strange Days"_s
                                       << u"1967 - Strange Days"_s << u"The Doors"_s;
        QTest::newRow("artist - album") << u"@@x\\Music\\The Doors - Strange Days"_s
                                        << u"The Doors - Strange Days"_s << u"The Doors"_s;
        QTest::newRow("year - album, generic parent") << u"@@x\\complete\\1967 - Strange Days"_s
                                                      << u"1967 - Strange Days"_s << QString();
        QTest::newRow("disc folder") << u"@@x\\Pink Floyd\\The Wall\\CD2"_s
                                     << u"The Wall"_s << u"Pink Floyd"_s;
        QTest::newRow("share root") << u"@@x\\Some Album"_s << u"Some Album"_s << QString();
    }

    void folderNames()
    {
        QFETCH(QString, folder);
        QFETCH(QString, album);
        QFETCH(QString, artist);
        const Slskd::FolderNames names = Slskd::namesFromFolder(folder);
        QCOMPARE(names.album, album);
        QCOMPARE(names.artist, artist);
    }

    void groupsByUserAndFolder()
    {
        auto make = [](const QString &user, const QString &path, qint64 size) {
            SlskdSearchFile file;
            file.username = user;
            file.filename = path;
            file.size = size;
            return file;
        };
        const QList<SlskdSearchFile> files{
            make(u"alice"_s, u"@@a\\Pink Floyd\\The Wall\\CD1\\01.flac"_s, 10),
            make(u"bob"_s, u"@@b\\Pink Floyd\\The Wall\\01.mp3"_s, 5),
            make(u"alice"_s, u"@@a\\Pink Floyd\\The Wall\\CD2\\01.flac"_s, 20),
            make(u"alice"_s, u"@@a\\Pink Floyd\\Animals\\01.flac"_s, 1),
        };
        const QList<Slskd::Album> albums = Slskd::groupByAlbum(files);
        QCOMPARE(albums.size(), 3);
        QCOMPARE(albums[0].username, u"alice"_s); // first-seen order kept
        QCOMPARE(albums[0].title, u"The Wall"_s);
        QCOMPARE(albums[0].files.size(), 2); // both discs in one album
        QCOMPARE(albums[0].totalSize, 30);
        QCOMPARE(albums[1].username, u"bob"_s); // same album, other user: separate source
        QCOMPARE(albums[2].title, u"Animals"_s);
        QCOMPARE(albums[2].artist, u"Pink Floyd"_s);
    }

    void quality()
    {
        SlskdSearchFile file;
        QCOMPARE(Slskd::qualityText(file), QString());
        file.bitRate = 320;
        QCOMPARE(Slskd::qualityText(file), u"320 kbps"_s);
        file.bitRate = 0;
        file.sampleRate = 44100;
        QCOMPARE(Slskd::qualityText(file), u"44.1 kHz"_s);
        file.bitDepth = 24;
        file.sampleRate = 96000;
        QCOMPARE(Slskd::qualityText(file), u"24-bit · 96 kHz"_s);
    }

    void filter()
    {
        SlskdSearchFile mp3;
        mp3.extension = u"mp3"_s;
        mp3.bitRate = 192;
        SlskdSearchFile cd;
        cd.extension = u"flac"_s; // bitRate 0 = unknown
        cd.bitDepth = 16;
        cd.sampleRate = 44100;
        SlskdSearchFile hiRes = cd;
        hiRes.bitDepth = 24;
        hiRes.sampleRate = 96000;
        SlskdSearchFile unknownFlac;
        unknownFlac.extension = u"flac"_s;

        QVERIFY(Slskd::matches(mp3, {}));
        QVERIFY(!Slskd::matches(mp3, {u"flac"_s, 0, 0, 0}));
        QVERIFY(!Slskd::matches(mp3, {{}, 320, 0, 0}));
        QVERIFY(Slskd::matches(cd, {{}, 320, 0, 0}));

        const SlskdSearchFilter cdQuality{u"flac"_s, 0, 16, 44100};
        const SlskdSearchFilter hiResQuality{u"flac"_s, 0, 24, 96000};
        QVERIFY(Slskd::matches(cd, cdQuality));
        QVERIFY(!Slskd::matches(hiRes, cdQuality));
        QVERIFY(Slskd::matches(hiRes, hiResQuality));
        QVERIFY(!Slskd::matches(cd, hiResQuality));
        QVERIFY(!Slskd::matches(unknownFlac, cdQuality));
        QVERIFY(Slskd::matches(unknownFlac, {u"flac"_s, 0, 0, 0}));
    }
};

QTEST_GUILESS_MAIN(TestSlskdClient)
#include "tst_slskdclient.moc"

#include "FakeHttpServer.h"
#include "core/LyricsFetcher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <optional>

using namespace Qt::StringLiterals;

namespace {

Track sampleTrack()
{
    Track track;
    track.title = u"Would?"_s;
    track.artist = u"Alice in Chains"_s;
    track.album = u"Dirt"_s;
    track.durationMs = 207'600;
    return track;
}

QByteArray record(const QString &synced, const QString &plain, double duration, bool instrumental = false)
{
    return QJsonDocument(QJsonObject{{"syncedLyrics"_L1, synced.isNull() ? QJsonValue() : QJsonValue(synced)},
                                     {"plainLyrics"_L1, plain.isNull() ? QJsonValue() : QJsonValue(plain)},
                                     {"duration"_L1, duration},
                                     {"instrumental"_L1, instrumental}})
        .toJson();
}

} // namespace

class TestLyricsFetcher : public QObject
{
    Q_OBJECT

private slots:
    void exactMatchPrefersSyncedAndCaches()
    {
        FakeHttpServer server;
        server.responseBody = record(u"[00:01.00]Know me broken"_s, u"Know me broken"_s, 207);
        QTemporaryDir cache;
        LyricsFetcher fetcher(cache.path());
        fetcher.setBaseUrl(server.url());

        std::optional<QString> result;
        fetcher.fetch(sampleTrack(), [&](const QString &lyrics) { result = lyrics; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(*result, u"[00:01.00]Know me broken"_s);

        const FakeHttpServer::Request &request = server.requests.first();
        QVERIFY(request.path.startsWith("/api/get?"));
        const QUrlQuery query(QUrl(QString::fromUtf8(request.path)).query());
        QCOMPARE(query.queryItemValue(u"track_name"_s, QUrl::FullyDecoded), u"Would?"_s);
        QCOMPARE(query.queryItemValue(u"duration"_s), u"208"_s); // rounded seconds
        QVERIFY(request.userAgent.startsWith("GRIM/"));

        bool known = false;
        QCOMPARE(fetcher.cached(sampleTrack(), &known), u"[00:01.00]Know me broken"_s);
        QVERIFY(known);
    }

    void fallsBackToSearchAndPicksBestMatch()
    {
        FakeHttpServer server;
        server.respond = [](const FakeHttpServer::Request &request) -> std::pair<int, QByteArray> {
            if (request.path.startsWith("/api/get"))
                return {404, "{}"};
            const QJsonArray results{
                QJsonDocument::fromJson(record(QString(), u"plain only"_s, 207)).object(),
                QJsonDocument::fromJson(record(u"[00:01.00]synced, wrong length"_s, QString(), 300)).object(),
                QJsonDocument::fromJson(record(u"[00:01.00]synced, same length"_s, QString(), 208)).object(),
            };
            return {200, QJsonDocument(results).toJson()};
        };
        QTemporaryDir cache;
        LyricsFetcher fetcher(cache.path());
        fetcher.setBaseUrl(server.url());

        std::optional<QString> result;
        fetcher.fetch(sampleTrack(), [&](const QString &lyrics) { result = lyrics; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(*result, u"[00:01.00]synced, same length"_s);
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(server.requests[1].path.startsWith("/api/search?"));
    }

    void remembersNothingFoundButNotOutages()
    {
        QTemporaryDir cache;
        {
            FakeHttpServer server;
            server.respond = [](const FakeHttpServer::Request &request) -> std::pair<int, QByteArray> {
                return request.path.startsWith("/api/get") ? std::pair<int, QByteArray>{404, "{}"}
                                                           : std::pair<int, QByteArray>{200, "[]"};
            };
            LyricsFetcher fetcher(cache.path());
            fetcher.setBaseUrl(server.url());
            std::optional<QString> result;
            fetcher.fetch(sampleTrack(), [&](const QString &lyrics) { result = lyrics; });
            QTRY_VERIFY(result.has_value());
            QVERIFY(result->isEmpty());
            bool known = false;
            fetcher.cached(sampleTrack(), &known);
            QVERIFY(known); // "nothing found" is remembered
        }

        // A server that is not there: empty result, and nothing cached for the other track.
        Track other = sampleTrack();
        other.title = u"Rooster"_s;
        LyricsFetcher fetcher(cache.path());
        fetcher.setBaseUrl(QUrl(u"http://127.0.0.1:1"_s));
        std::optional<QString> result;
        fetcher.fetch(other, [&](const QString &lyrics) { result = lyrics; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 15000);
        QVERIFY(result->isEmpty());
        bool known = true;
        fetcher.cached(other, &known);
        QVERIFY(!known);
    }

    void instrumentalHasNoLyrics()
    {
        FakeHttpServer server;
        server.responseBody = record(QString(), QString(), 200, true);
        QTemporaryDir cache;
        LyricsFetcher fetcher(cache.path());
        fetcher.setBaseUrl(server.url());
        std::optional<QString> result;
        fetcher.fetch(sampleTrack(), [&](const QString &lyrics) { result = lyrics; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(result->isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLyricsFetcher)
#include "tst_lyricsfetcher.moc"

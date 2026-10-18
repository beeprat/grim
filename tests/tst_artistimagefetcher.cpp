#include "FakeHttpServer.h"
#include "core/ArtistImageFetcher.h"

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <optional>

using namespace Qt::StringLiterals;

namespace {

QByteArray jpeg()
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG");
    return bytes;
}

QByteArray searchAnswer(const QJsonArray &artists)
{
    return QJsonDocument(QJsonObject{{"data"_L1, artists}}).toJson();
}

} // namespace

class TestArtistImageFetcher : public QObject
{
    Q_OBJECT

private slots:
    void prefersExactNameWithMostFansAndCachesThePhoto()
    {
        FakeHttpServer server;
        const QString base = server.url().toString();
        server.respond = [base](const FakeHttpServer::Request &request) -> std::pair<int, QByteArray> {
            if (request.path.startsWith("/search/artist")) {
                return {200, searchAnswer({
                                 QJsonObject{{"name"_L1, "Alice in Chains Tribute"}, {"picture_big"_L1, base + u"/wrong.jpg"_s}},
                                 QJsonObject{{"name"_L1, "Alice in Chains"}, {"nb_fan"_L1, 14}, {"picture_big"_L1, base + u"/namesake.jpg"_s}},
                                 QJsonObject{{"name"_L1, "alice in chains"}, {"nb_fan"_L1, 1301454}, {"picture_big"_L1, base + u"/right.jpg"_s}},
                             })};
            }
            if (request.path == "/right.jpg")
                return {200, jpeg()};
            return {404, {}};
        };
        QTemporaryDir cache;
        ArtistImageFetcher fetcher(cache.path());
        fetcher.setBaseUrl(server.url());

        std::optional<QUrl> result;
        fetcher.fetch(u"Alice in Chains"_s, [&](const QUrl &file) { result = file; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(result->isLocalFile());
        QVERIFY(!QImage(result->toLocalFile()).isNull());

        const QUrlQuery query(QUrl(QString::fromUtf8(server.requests.first().path)).query());
        QCOMPARE(query.queryItemValue(u"q"_s, QUrl::FullyDecoded), u"Alice in Chains"_s);
        QCOMPARE(server.requests.last().path, QByteArray("/right.jpg"));

        bool known = false;
        QCOMPARE(fetcher.cached(u"ALICE IN CHAINS"_s, &known), *result);
        QVERIFY(known);
    }

    void placeholderPhotoCountsAsNone()
    {
        FakeHttpServer server;
        server.responseBody = searchAnswer({QJsonObject{
            {"name"_L1, "Nobody"},
            {"picture_big"_L1, "https://cdn-images.dzcdn.net/images/artist//500x500-000000-80-0-0.jpg"}}});
        QTemporaryDir cache;
        ArtistImageFetcher fetcher(cache.path());
        fetcher.setBaseUrl(server.url());

        std::optional<QUrl> result;
        fetcher.fetch(u"Nobody"_s, [&](const QUrl &file) { result = file; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(result->isEmpty());
        QCOMPARE(server.requests.size(), 1); // no image download

        bool known = false;
        QVERIFY(fetcher.cached(u"Nobody"_s, &known).isEmpty());
        QVERIFY(known);
    }

    void serviceFailureIsNotCached()
    {
        FakeHttpServer server;
        server.status = 500;
        QTemporaryDir cache;
        ArtistImageFetcher fetcher(cache.path());
        fetcher.setBaseUrl(server.url());

        std::optional<QUrl> result;
        fetcher.fetch(u"Camel"_s, [&](const QUrl &file) { result = file; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(result->isEmpty());

        bool known = true;
        fetcher.cached(u"Camel"_s, &known);
        QVERIFY(!known);
    }
};

QTEST_GUILESS_MAIN(TestArtistImageFetcher)
#include "tst_artistimagefetcher.moc"

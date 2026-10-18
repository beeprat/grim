#include "core/LastFmClient.h"

#include "FakeHttpServer.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace {

// Form body of a recorded request, decoded.
QUrlQuery formOf(const FakeHttpServer::Request &request)
{
    QUrlQuery query;
    for (const QByteArray &pair : request.body.split('&')) {
        const qsizetype equals = pair.indexOf('=');
        query.addQueryItem(QUrl::fromPercentEncoding(pair.left(equals)), QUrl::fromPercentEncoding(pair.mid(equals + 1)));
    }
    return query;
}

LastFmClient::Scrobble sample(const QString &title)
{
    return {u"Sigur Rós"_s, title, u"Takk..."_s, 268000, 1760000000};
}

} // namespace

class TestLastFmClient : public QObject
{
    Q_OBJECT

private slots:
    void signatureSortsParamsAndSkipsFormat()
    {
        // md5("api_keyKEY" "methodtrack.scrobble" "track[0]Café + Bar&Baz" "SECRET"), computed outside Qt.
        const QMap<QString, QString> params{{u"track[0]"_s, u"Café + Bar&Baz"_s},
                                            {u"method"_s, u"track.scrobble"_s},
                                            {u"api_key"_s, u"KEY"_s},
                                            {u"format"_s, u"json"_s}};
        QCOMPARE(LastFmClient::signature(params, u"SECRET"_s), u"35d8f414136648ceadd8de8d3540e5d2"_s);
    }

    void scrobbleIsSignedBatchedAndLeavesQueue()
    {
        QTemporaryDir dir;
        const QString queueFile = dir.filePath(u"queue.json"_s);
        FakeHttpServer server;
        server.responseBody = R"({"scrobbles":{"@attr":{"accepted":1,"ignored":0}}})";

        LastFmClient client(queueFile);
        client.setBaseUrl(server.url());
        client.setApiKey(u"KEY"_s, u"SECRET"_s);
        client.setSessionKey(u"SK"_s);
        client.enqueue(sample(u"Hoppípolla"_s));
        QTRY_COMPARE(client.pendingCount(), 0);

        QCOMPARE(server.requests.size(), 1);
        const QUrlQuery form = formOf(server.requests.first());
        QCOMPARE(form.queryItemValue(u"method"_s), u"track.scrobble"_s);
        QCOMPARE(form.queryItemValue(u"artist[0]"_s), u"Sigur Rós"_s);
        QCOMPARE(form.queryItemValue(u"track[0]"_s), u"Hoppípolla"_s);
        QCOMPARE(form.queryItemValue(u"timestamp[0]"_s), u"1760000000"_s);
        QCOMPARE(form.queryItemValue(u"duration[0]"_s), u"268"_s);
        QCOMPARE(form.queryItemValue(u"sk"_s), u"SK"_s);
        QCOMPARE(form.queryItemValue(u"format"_s), u"json"_s);

        QMap<QString, QString> signedParams;
        for (const auto &[name, value] : form.queryItems(QUrl::FullyDecoded))
            if (name != "api_sig"_L1)
                signedParams.insert(name, value);
        QCOMPARE(form.queryItemValue(u"api_sig"_s), LastFmClient::signature(signedParams, u"SECRET"_s));

        // The emptied queue is persisted too.
        LastFmClient reopened(queueFile);
        QCOMPARE(reopened.pendingCount(), 0);
    }

    void offlineScrobblesSurviveRestart()
    {
        QTemporaryDir dir;
        const QString queueFile = dir.filePath(u"queue.json"_s);
        {
            LastFmClient client(queueFile);
            client.setBaseUrl(QUrl(u"http://127.0.0.1:1"_s)); // nothing listens: connection refused
            client.setApiKey(u"KEY"_s, u"SECRET"_s);
            client.setSessionKey(u"SK"_s);
            QSignalSpy errors(&client, &LastFmClient::errorOccurred);
            client.enqueue(sample(u"Glósóli"_s));
            QTRY_COMPARE(errors.size(), 1);
            QCOMPARE(client.pendingCount(), 1);
        }

        FakeHttpServer server;
        LastFmClient client(queueFile);
        QCOMPARE(client.pendingCount(), 1);
        client.setBaseUrl(server.url());
        client.setApiKey(u"KEY"_s, u"SECRET"_s);
        client.setSessionKey(u"SK"_s);
        client.flush();
        QTRY_COMPARE(client.pendingCount(), 0);
        QCOMPARE(formOf(server.requests.first()).queryItemValue(u"track[0]"_s), u"Glósóli"_s);
    }

    void revokedSessionKeepsQueue()
    {
        QTemporaryDir dir;
        FakeHttpServer server;
        server.status = 403;
        server.responseBody = R"({"error":9,"message":"Invalid session key - Please re-authenticate"})";

        LastFmClient client(dir.filePath(u"queue.json"_s));
        client.setBaseUrl(server.url());
        client.setApiKey(u"KEY"_s, u"SECRET"_s);
        client.setSessionKey(u"SK"_s);
        QSignalSpy rejected(&client, &LastFmClient::sessionRejected);
        client.enqueue(sample(u"Sæglópur"_s));
        QTRY_COMPARE(rejected.size(), 1);
        QCOMPARE(client.pendingCount(), 1);
    }

    void sessionFromToken()
    {
        FakeHttpServer server;
        server.responseBody = R"({"session":{"name":"listener","key":"SESSION","subscriber":0}})";
        QTemporaryDir dir;
        LastFmClient client(dir.filePath(u"queue.json"_s));
        client.setBaseUrl(server.url());
        client.setApiKey(u"KEY"_s, u"SECRET"_s);

        QString user;
        QString key;
        client.requestSession(u"TOKEN"_s, [&](const QString &u, const QString &k) {
            user = u;
            key = k;
        });
        QTRY_COMPARE(key, u"SESSION"_s);
        QCOMPARE(user, u"listener"_s);
        const QUrlQuery form = formOf(server.requests.first());
        QCOMPARE(form.queryItemValue(u"method"_s), u"auth.getSession"_s);
        QCOMPARE(form.queryItemValue(u"token"_s), u"TOKEN"_s);
    }
};

QTEST_GUILESS_MAIN(TestLastFmClient)
#include "tst_lastfmclient.moc"

#include "core/DiscordIpc.h"
#include "core/ImgurUploader.h"
#include "core/KeyFile.h"

#include "FakeHttpServer.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace {

QByteArray frame(qint32 op, const QJsonObject &payload)
{
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray bytes(8, '\0');
    qToLittleEndian<qint32>(op, bytes.data());
    qToLittleEndian<qint32>(static_cast<qint32>(body.size()), bytes.data() + 4);
    return bytes + body;
}

// Splits everything received into (op, json) frames.
QList<QPair<int, QJsonObject>> frames(QByteArray bytes)
{
    QList<QPair<int, QJsonObject>> result;
    while (bytes.size() >= 8) {
        const qint32 length = qFromLittleEndian<qint32>(bytes.constData() + 4);
        if (bytes.size() < 8 + length)
            break;
        result.append({qFromLittleEndian<qint32>(bytes.constData()),
                       QJsonDocument::fromJson(bytes.mid(8, length)).object()});
        bytes.remove(0, 8 + length);
    }
    return result;
}

} // namespace

class TestDiscord : public QObject
{
    Q_OBJECT

private slots:
    void keyFileIgnoresCommentsAndSpacing()
    {
        const QHash<QString, QString> keys = KeyFile::parse(QStringLiteral(
            "# comment = not a key\n\n  Application_ID =  1234 \nimgur_client_id=abc\r\nbroken line\n=no name\n"));
        QCOMPARE(keys.size(), 2);
        QCOMPARE(keys.value(QStringLiteral("application_id")), QStringLiteral("1234"));
        QCOMPARE(keys.value(QStringLiteral("imgur_client_id")), QStringLiteral("abc"));
        QVERIFY(KeyFile::read(QStringLiteral("does-not-exist.txt")).isEmpty());
    }

    void handshakeThenActivity()
    {
        // Plays Discord's side of the pipe.
        const QString base = u"grim-test-ipc-%1-"_s.arg(QCoreApplication::applicationPid());
        QLocalServer server;
        QVERIFY(server.listen(base + u"0"_s));
        QLocalSocket *client = nullptr;
        QByteArray received;
        connect(&server, &QLocalServer::newConnection, this, [&] {
            client = server.nextPendingConnection();
            connect(client, &QLocalSocket::readyRead, this, [&] { received += client->readAll(); });
        });

        DiscordIpc ipc;
        ipc.setPipeBaseName(base);
        ipc.setActivity(QJsonObject{{"type"_L1, 2}, {"state"_L1, u"Sigur Rós"_s}}); // waits for READY
        ipc.start(u"123456"_s);

        QTRY_COMPARE(frames(received).size(), 1);
        const auto handshake = frames(received).first();
        QCOMPARE(handshake.first, 0);
        QCOMPARE(handshake.second.value("client_id"_L1).toString(), u"123456"_s);
        QCOMPARE(handshake.second.value("v"_L1).toInt(), 1);
        QVERIFY(!ipc.isReady());

        client->write(frame(1, QJsonObject{{"cmd"_L1, "DISPATCH"_L1}, {"evt"_L1, "READY"_L1}}));
        QTRY_VERIFY(ipc.isReady());
        QTRY_COMPARE(frames(received).size(), 2);
        const auto set = frames(received).at(1);
        QCOMPARE(set.first, 1);
        QCOMPARE(set.second.value("cmd"_L1).toString(), u"SET_ACTIVITY"_s);
        const QJsonObject args = set.second.value("args"_L1).toObject();
        QCOMPARE(args.value("pid"_L1).toInteger(), QCoreApplication::applicationPid());
        QCOMPARE(args.value("activity"_L1).toObject().value("state"_L1).toString(), u"Sigur Rós"_s);

        // Ping is answered with a pong carrying the same payload.
        client->write(frame(3, QJsonObject{{"n"_L1, 7}}));
        QTRY_COMPARE(frames(received).size(), 3);
        QCOMPARE(frames(received).at(2).first, 4);
        QCOMPARE(frames(received).at(2).second.value("n"_L1).toInt(), 7);

        // An empty activity clears the presence: no "activity" key.
        ipc.setActivity({});
        QTRY_COMPARE(frames(received).size(), 4);
        QVERIFY(!frames(received).at(3).second.value("args"_L1).toObject().contains("activity"_L1));
    }

    void imgurUploadsEachImageOnce()
    {
        QTemporaryDir dir;
        const QString cacheFile = dir.filePath(u"imgur.json"_s);
        FakeHttpServer server;
        server.responseBody = R"({"data":{"link":"https://i.imgur.com/abc.jpg","deletehash":"del"},"success":true,"status":200})";

        ImgurUploader uploader(cacheFile);
        uploader.setEndpoint(server.url());
        uploader.setClientId(u"CID"_s);
        const QByteArray image("\xff\xd8 fake jpeg + / bytes", 22);

        // Two requests for the same image while the first upload runs: one upload.
        QString first;
        QString second;
        uploader.upload(image, [&](const QString &url) { first = url; });
        uploader.upload(image, [&](const QString &url) { second = url; });
        QTRY_COMPARE(first, u"https://i.imgur.com/abc.jpg"_s);
        QCOMPARE(second, first);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests.first().authorization, QByteArray("Client-ID CID"));
        const QByteArray body = server.requests.first().body;
        QVERIFY(body.startsWith("type=base64&image="));
        QCOMPARE(QByteArray::fromBase64(QByteArray::fromPercentEncoding(body.mid(18))), image);

        // Known after a restart, without another upload.
        ImgurUploader reopened(cacheFile);
        QCOMPARE(reopened.cachedUrl(ImgurUploader::keyOf(image)), first);
        QString cached;
        reopened.upload(image, [&](const QString &url) { cached = url; });
        QCOMPARE(cached, first);
        QCOMPARE(server.requests.size(), 1);
    }

    void imgurFailureIsNotCached()
    {
        QTemporaryDir dir;
        FakeHttpServer server;
        server.status = 429;
        server.responseBody = R"({"data":{"error":"Too Many Requests"},"success":false,"status":429})";
        ImgurUploader uploader(dir.filePath(u"imgur.json"_s));
        uploader.setEndpoint(server.url());
        uploader.setClientId(u"CID"_s);

        bool done = false;
        QString url = u"unset"_s;
        uploader.upload("img", [&](const QString &u) {
            url = u;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(url.isEmpty());
        QVERIFY(uploader.cachedUrl(ImgurUploader::keyOf("img")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDiscord)
#include "tst_discord.moc"

#pragma once

#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>
#include <utility>

// Minimal HTTP/1.1 server for tests: records each request and answers with a fixed
// status and body, or with whatever `respond` returns for that request.
class FakeHttpServer : public QObject
{
public:
    struct Request
    {
        QByteArray method;
        QByteArray path; // includes the query string
        QByteArray apiKey; // X-API-Key header
        QByteArray userAgent;
        QByteArray authorization;
        QByteArray body;
    };

    QList<Request> requests;
    int status = 200;
    QByteArray responseBody = "{}";
    // Optional per-request answer: {status, body}. Overrides status/responseBody.
    std::function<std::pair<int, QByteArray>(const Request &)> respond;

    FakeHttpServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    m_buffers[socket] += socket->readAll();
                    respondIfComplete(socket);
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }

private:
    void respondIfComplete(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        Request request;
        qsizetype contentLength = 0;
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
        request.method = requestLine.value(0);
        request.path = requestLine.value(1);
        for (const QByteArray &line : lines.mid(1)) {
            const qsizetype colon = line.indexOf(':');
            const QByteArray name = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (name == "content-length")
                contentLength = value.toLongLong();
            else if (name == "x-api-key")
                request.apiKey = value;
            else if (name == "user-agent")
                request.userAgent = value;
            else if (name == "authorization")
                request.authorization = value;
        }
        if (buffer.size() < headerEnd + 4 + contentLength)
            return;
        request.body = buffer.mid(headerEnd + 4, contentLength);
        requests.append(request);
        m_buffers.remove(socket);

        const auto [code, body] = respond ? respond(request) : std::pair<int, QByteArray>{status, responseBody};
        socket->write("HTTP/1.1 " + QByteArray::number(code) + " X\r\nContent-Type: application/json\r\n"
                      "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

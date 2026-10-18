#include "core/DiscordIpc.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QDir>
#include <QLocalSocket>
#include <QTimer>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace {

constexpr int kPipeCount = 10;
constexpr int kRetryMs = 15000; // Discord not running: look again later

// Where the desktop client listens. Windows: named pipes (Qt adds the \\.\pipe\ prefix). Linux: a Unix
// socket in $XDG_RUNTIME_DIR, or in its Flatpak/Snap subfolder; the folder holding a socket right now wins.
QString defaultPipeBaseName()
{
#ifdef Q_OS_WIN
    return u"discord-ipc-"_s;
#else
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        runtime = u"/tmp"_s;
    const QStringList folders{runtime, runtime + u"/app/com.discordapp.Discord"_s, runtime + u"/snap.discord"_s};
    for (const QString &folder : folders) {
        if (!QDir(folder).entryList({u"discord-ipc-*"_s}, QDir::System | QDir::Files).isEmpty())
            return folder + u"/discord-ipc-"_s;
    }
    return folders.first() + u"/discord-ipc-"_s;
#endif
}

} // namespace

DiscordIpc::DiscordIpc(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
    , m_retry(new QTimer(this))
{
    m_retry->setSingleShot(true);
    m_retry->setInterval(kRetryMs);
    connect(m_retry, &QTimer::timeout, this, [this] {
        m_pipeIndex = 0;
        tryNextPipe();
    });
    connect(m_socket, &QLocalSocket::connected, this, &DiscordIpc::onConnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &DiscordIpc::onReadyRead);
    connect(m_socket, &QLocalSocket::disconnected, this, &DiscordIpc::onDisconnected);
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this] {
        if (m_socket->state() == QLocalSocket::UnconnectedState && !m_ready && !m_clientId.isEmpty()) {
            ++m_pipeIndex; // this pipe does not exist: try the next one
            tryNextPipe();
        }
    });
}

DiscordIpc::~DiscordIpc()
{
    stop();
}

void DiscordIpc::setPipeBaseName(const QString &baseName)
{
    m_baseName = baseName;
}

void DiscordIpc::start(const QString &clientId)
{
    stop();
    m_clientId = clientId.trimmed();
    if (m_clientId.isEmpty())
        return;
    m_pipeIndex = 0;
    tryNextPipe();
}

void DiscordIpc::stop()
{
    m_clientId.clear();
    m_retry->stop();
    m_socket->abort();
    m_buffer.clear();
    setReady(false);
}

bool DiscordIpc::isReady() const
{
    return m_ready;
}

void DiscordIpc::setActivity(const QJsonObject &activity)
{
    m_activity = activity;
    if (m_ready)
        sendActivity();
}

void DiscordIpc::tryNextPipe()
{
    if (m_clientId.isEmpty())
        return;
    if (m_pipeIndex >= kPipeCount) {
        m_retry->start();
        return;
    }
    m_socket->connectToServer((m_baseName.isEmpty() ? defaultPipeBaseName() : m_baseName)
                              + QString::number(m_pipeIndex));
}

void DiscordIpc::onConnected()
{
    m_buffer.clear();
    send(Handshake, QJsonObject{{"v"_L1, 1}, {"client_id"_L1, m_clientId}});
}

void DiscordIpc::onReadyRead()
{
    m_buffer += m_socket->readAll();
    while (m_buffer.size() >= 8) {
        const int op = qFromLittleEndian<qint32>(m_buffer.constData());
        const qint32 length = qFromLittleEndian<qint32>(m_buffer.constData() + 4);
        if (length < 0 || m_buffer.size() < 8 + length)
            return;
        const QByteArray body = m_buffer.mid(8, length);
        m_buffer.remove(0, 8 + length);
        const QJsonObject message = QJsonDocument::fromJson(body).object();

        if (op == Ping) {
            send(Pong, message);
        } else if (op == Close) {
            // Usually a wrong application ID; Discord closes the pipe after this.
            qInfo("DiscordIpc: closed by Discord: %s", qPrintable(message.value("message"_L1).toString()));
            m_socket->disconnectFromServer();
        } else if (op == Frame && message.value("evt"_L1).toString() == "READY"_L1) {
            setReady(true);
            sendActivity();
        } else if (op == Frame && message.value("evt"_L1).toString() == "ERROR"_L1) {
            // Discord rejected a command (usually the activity); it says why in data.message.
            const QString text = message.value("data"_L1).toObject().value("message"_L1).toString();
            qWarning("DiscordIpc: %s", qPrintable(text));
            emit errorOccurred(text);
        } else if (op == Frame) {
            qInfo("DiscordIpc: %s", QJsonDocument(message).toJson(QJsonDocument::Compact).constData());
        }
    }
}

void DiscordIpc::onDisconnected()
{
    setReady(false);
    if (!m_clientId.isEmpty())
        m_retry->start();
}

void DiscordIpc::send(int op, const QJsonObject &payload)
{
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    char header[8];
    qToLittleEndian<qint32>(op, header);
    qToLittleEndian<qint32>(static_cast<qint32>(body.size()), header + 4);
    m_socket->write(header, sizeof header);
    m_socket->write(body);
}

void DiscordIpc::sendActivity()
{
    QJsonObject args{{"pid"_L1, static_cast<qint64>(QCoreApplication::applicationPid())}};
    if (!m_activity.isEmpty())
        args.insert("activity"_L1, m_activity); // no "activity" key clears the presence
    send(Frame, QJsonObject{{"cmd"_L1, "SET_ACTIVITY"_L1}, {"args"_L1, args}, {"nonce"_L1, QString::number(++m_nonce)}});
}

void DiscordIpc::setReady(bool ready)
{
    if (ready == m_ready)
        return;
    m_ready = ready;
    emit readyChanged();
}

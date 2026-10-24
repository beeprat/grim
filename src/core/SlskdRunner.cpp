#include "core/SlskdRunner.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QtCore/private/qzipreader_p.h>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#include <sys/prctl.h>
#endif

using namespace Qt::StringLiterals;

namespace {

// The official release build for this system and its SHA-256 (as GitHub lists it). To upgrade slskd,
// change kVersion and both hashes together.
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
constexpr auto kAsset = "win-x64";
constexpr auto kSha256 = "ffe186ecbedce15bf19b6a690c6213c669f34acc793ef53aa92390a5cd9c48f0";
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
constexpr auto kAsset = "linux-x64";
constexpr auto kSha256 = "53102e91e0123f62d17627d5e02a24d9dcd08a165e75182c0f1d76d087510f37";
#else
constexpr auto kAsset = "";
constexpr auto kSha256 = "";
#endif

constexpr int kMaxRetries = 3;
constexpr int kRetryDelayMs = 5000;

} // namespace

SlskdRunner::SlskdRunner(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_retryTimer(new QTimer(this))
{
    m_retryTimer->setSingleShot(true);
    m_retryTimer->setInterval(kRetryDelayMs);
    connect(m_retryTimer, &QTimer::timeout, this, &SlskdRunner::launch);
    m_state = QFile::exists(executable()) ? Stopped : NotInstalled;

#ifdef Q_OS_WIN
    // Every process in this job is killed when grim. exits, however it exits.
    m_job = CreateJobObjectW(nullptr, nullptr);
    if (m_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
#endif
}

SlskdRunner::~SlskdRunner()
{
    stop();
#ifdef Q_OS_WIN
    if (m_job)
        CloseHandle(m_job);
#endif
}

SlskdRunner::State SlskdRunner::state() const
{
    return m_state;
}

double SlskdRunner::progress() const
{
    return m_progress;
}

QString SlskdRunner::errorText() const
{
    return m_errorText;
}

QString SlskdRunner::serverUrl() const
{
    return u"http://127.0.0.1:%1"_s.arg(kPort);
}

QString SlskdRunner::dataDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/slskd/data"_s;
}

QString SlskdRunner::installDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/slskd/"_s
           + QLatin1StringView(kVersion);
}

QString SlskdRunner::executable() const
{
#ifdef Q_OS_WIN
    return installDirectory() + u"/slskd.exe"_s;
#else
    return installDirectory() + u"/slskd"_s;
#endif
}

void SlskdRunner::setState(State state, const QString &error)
{
    if (state == m_state && error == m_errorText)
        return;
    m_state = state;
    m_errorText = error;
    emit stateChanged();
}

void SlskdRunner::install()
{
    if (m_state == Downloading || QFile::exists(executable()))
        return;
    if (QLatin1StringView(kAsset).isEmpty()) {
        setState(Failed, tr("slskd can't be set up automatically on this system. Run your own slskd instead."));
        return;
    }
    QDir().mkpath(installDirectory() + u"/.."_s);
    const QString zipPath = installDirectory() + u".zip.part"_s;
    auto *file = new QFile(zipPath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        file->deleteLater();
        setState(Failed, tr("Could not write %1").arg(QDir::toNativeSeparators(zipPath)));
        return;
    }

    const QUrl url(u"https://github.com/slskd/slskd/releases/download/%1/slskd-%1-%2.zip"_s
                       .arg(QLatin1StringView(kVersion), QLatin1StringView(kAsset)));
    QNetworkRequest request(url);
    request.setTransferTimeout(60000);
    QNetworkReply *reply = m_network->get(request); // follows GitHub's https redirect to its file host
    m_progress = 0;
    setState(Downloading);
    // About 55 MB: written to disk as it arrives rather than held in memory.
    connect(reply, &QNetworkReply::readyRead, this, [reply, file] { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_progress = total > 0 ? double(received) / double(total) : 0;
        emit stateChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, zipPath] {
        reply->deleteLater();
        file->write(reply->readAll());
        file->close();
        file->deleteLater();
        QString error;
        if (reply->error() != QNetworkReply::NoError)
            error = tr("Download failed: %1").arg(reply->errorString());
        else
            installFromZip(zipPath, kSha256, installDirectory(), &error);
        QFile::remove(zipPath);
        setState(error.isEmpty() ? Stopped : Failed, error);
    });
}

bool SlskdRunner::installFromZip(const QString &zipPath, const QByteArray &sha256Hex, const QString &destination,
                                 QString *error)
{
    QFile zip(zipPath);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!zip.open(QIODevice::ReadOnly) || !hash.addData(&zip)) {
        *error = tr("Could not read the slskd download.");
        return false;
    }
    zip.close();
    if (hash.result().toHex() != sha256Hex.toLower()) {
        *error = tr("The slskd download is damaged or not the expected file.");
        return false;
    }

    // Unpacked next to the destination, then renamed: a half-unpacked copy is never mistaken for an install.
    const QString unpacking = destination + u".unpacking"_s;
    QDir(unpacking).removeRecursively();
    QZipReader reader(zipPath);
    if (reader.status() != QZipReader::NoError || !reader.extractAll(unpacking)) {
        QDir(unpacking).removeRecursively();
        *error = tr("Could not unpack slskd.");
        return false;
    }
    QDir(destination).removeRecursively();
    if (!QDir().rename(unpacking, destination)) {
        QDir(unpacking).removeRecursively();
        *error = tr("Could not install slskd to %1").arg(QDir::toNativeSeparators(destination));
        return false;
    }
#ifndef Q_OS_WIN
    QFile::setPermissions(destination + u"/slskd"_s,
                          QFile::permissions(destination + u"/slskd"_s) | QFile::ExeOwner | QFile::ReadOwner);
#endif
    return true;
}

void SlskdRunner::start(const QString &username, const QString &password, const QString &apiKey)
{
    m_username = username;
    m_password = password;
    m_apiKey = apiKey;
    m_retries = 0;
    stop();
    launch();
}

void SlskdRunner::launch()
{
    if (!QFile::exists(executable())) {
        setState(NotInstalled);
        return;
    }
    QDir().mkpath(dataDirectory());

    m_process = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Settings as environment variables: nothing secret on the command line, and the user's own slskd
    // settings (if any) are not touched: --app-dir gives this copy its own folder.
    env.insert(u"SLSKD_HTTP_IP_ADDRESS"_s, u"127.0.0.1"_s);
    env.insert(u"SLSKD_HTTP_PORT"_s, QString::number(kPort));
    env.insert(u"SLSKD_NO_HTTPS"_s, u"true"_s);
    env.insert(u"SLSKD_NO_AUTH"_s, u"false"_s);
    env.insert(u"SLSKD_API_KEY"_s, u"cidr=127.0.0.1/32;"_s + m_apiKey);
    env.insert(u"SLSKD_SLSK_USERNAME"_s, m_username);
    env.insert(u"SLSKD_SLSK_PASSWORD"_s, m_password);
    env.insert(u"SLSKD_NO_LOGO"_s, u"true"_s);
    env.insert(u"SLSKD_NO_VERSION_CHECK"_s, u"true"_s); // grim. pins the version
    env.insert(u"SLSKD_HEADLESS"_s, u"true"_s);
    m_process->setProcessEnvironment(env);
    m_process->setProgram(executable());
    m_process->setArguments({u"--app-dir"_s, QDir::toNativeSeparators(dataDirectory())});
    m_process->setStandardOutputFile(QProcess::nullDevice()); // slskd keeps its own log in the data folder
    m_process->setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW; // a console program: no console window
    });
#else
    m_process->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
#endif

    QProcess *process = m_process;
    connect(process, &QProcess::started, this, [this, process] {
#ifdef Q_OS_WIN
        if (m_job) {
            if (HANDLE handle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process->processId()))) {
                AssignProcessToJobObject(m_job, handle);
                CloseHandle(handle);
            }
        }
#else
        Q_UNUSED(process)
#endif
        setState(Running);
    });
    connect(process, &QProcess::finished, this, [this, process](int exitCode) {
        process->deleteLater();
        if (process != m_process)
            return; // stopped on purpose
        m_process = nullptr;
        if (++m_retries <= kMaxRetries) {
            m_retryTimer->start();
            return;
        }
        setState(Failed, tr("slskd stopped (exit code %1). Its log is in %2.")
                             .arg(exitCode)
                             .arg(QDir::toNativeSeparators(dataDirectory() + u"/logs"_s)));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || process != m_process)
            return;
        m_process = nullptr;
        process->deleteLater();
        setState(Failed, tr("Could not start slskd: %1").arg(process->errorString()));
    });
    m_process->start();
}

void SlskdRunner::stop()
{
    m_retryTimer->stop();
    if (!m_process) {
        if (m_state == Running)
            setState(Stopped);
        return;
    }
    QProcess *process = m_process;
    m_process = nullptr; // its finished handler now ignores the exit
#ifndef Q_OS_WIN
    process->terminate(); // slskd shuts down cleanly on SIGTERM
    if (!process->waitForFinished(3000))
#endif
        process->kill(); // Windows: a console program without a window has no gentler signal
    process->waitForFinished(3000);
    process->deleteLater();
    if (m_state == Running)
        setState(QFile::exists(executable()) ? Stopped : NotInstalled);
}

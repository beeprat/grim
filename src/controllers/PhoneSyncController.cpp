#include "PhoneSyncController.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QKeyEvent>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtConcurrentRun>

using namespace Qt::StringLiterals;

namespace {

constexpr auto kAppId = "com.plastaq.music1"; // MiniMoon
constexpr int kMountTimeoutMs = 15'000;
constexpr auto kSkipDuplicatesKey = "sync/skipDuplicates";
// SHA-256 of the unlock code (ten digits).
constexpr auto kUnlockHash = "8a78194e858435c8be4f84db15d78dd966889ed0da722d6f06aa1f243aeabf64";
constexpr int kUnlockLength = 10;

// Runs a program to the end; false when it is missing, times out or fails.
bool run(const QString &program, const QStringList &arguments, QString *output = nullptr)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForFinished(kMountTimeoutMs)) {
        process.kill();
        return false;
    }
    if (output)
        *output = QString::fromLocal8Bit(process.readAll()).trimmed();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} // namespace

PhoneSyncController::PhoneSyncController(QObject *parent)
    : QObject(parent)
    , m_skipDuplicates(QSettings().value(kSkipDuplicatesKey, false).toBool())
{
    if (supported())
        QCoreApplication::instance()->installEventFilter(this);
}

bool PhoneSyncController::unlocked() const
{
    return m_unlocked;
}

bool PhoneSyncController::eventFilter(QObject *watched, QEvent *event)
{
    // Each key press once: as it reaches the window (it is offered again to every item it passes).
    if (event->type() != QEvent::KeyPress || !watched->isWindowType())
        return false;
    const QString text = static_cast<QKeyEvent *>(event)->text();
    if (text.size() != 1 || !text.front().isDigit()) {
        m_typed.clear();
        return false;
    }
    m_typed = (m_typed + text).right(kUnlockLength);
    if (QCryptographicHash::hash(m_typed.toLatin1(), QCryptographicHash::Sha256).toHex() == kUnlockHash) {
        m_unlocked = true;
        QCoreApplication::instance()->removeEventFilter(this);
        emit unlockedChanged();
    }
    return false;
}

QStringList PhoneSyncController::added() const
{
    return m_plan.added;
}

QStringList PhoneSyncController::updated() const
{
    return m_plan.updated;
}

QStringList PhoneSyncController::removed() const
{
    return m_plan.remove;
}

QStringList PhoneSyncController::duplicates() const
{
    return m_plan.duplicates;
}

bool PhoneSyncController::skipDuplicates() const
{
    return m_skipDuplicates;
}

void PhoneSyncController::setSkipDuplicates(bool skip)
{
    if (skip == m_skipDuplicates)
        return;
    m_skipDuplicates = skip;
    QSettings().setValue(kSkipDuplicatesKey, skip);
    emit skipDuplicatesChanged();
}

PhoneSyncController::~PhoneSyncController()
{
    m_cancel = true;
    m_worker.waitForFinished();
    unmount();
}

PhoneSyncController::State PhoneSyncController::state() const
{
    return m_state;
}

QString PhoneSyncController::message() const
{
    return m_message;
}

int PhoneSyncController::copyCount() const
{
    return int(m_plan.copy.size());
}

int PhoneSyncController::removeCount() const
{
    return int(m_plan.remove.size());
}

qint64 PhoneSyncController::copyBytes() const
{
    return m_plan.copyBytes;
}

qreal PhoneSyncController::progress() const
{
    return m_progress;
}

bool PhoneSyncController::supported() const
{
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

void PhoneSyncController::setState(State state, const QString &message)
{
    m_state = state;
    m_message = message;
    emit stateChanged();
}

QString PhoneSyncController::mountPoint() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/iphone"_s;
}

QString PhoneSyncController::mount()
{
    QString devices;
    if (!run(u"idevice_id"_s, {u"-l"_s}, &devices))
        return tr("The iPhone tools are missing. Install them: sudo apt install libimobiledevice-utils ifuse");
    if (devices.isEmpty())
        return tr("No iPhone found. Plug it in with a cable, unlock it and tap Trust.");
    unmount(); // a mount left over from a crash
    QDir().mkpath(mountPoint());
    QString output;
    if (!run(u"ifuse"_s, {u"--documents"_s, QString::fromLatin1(kAppId), mountPoint()}, &output))
        return output.contains("not installed"_L1, Qt::CaseInsensitive) || output.contains("house_arrest"_L1)
                   ? tr("MiniMoon is not installed on this iPhone.")
                   : tr("Could not open MiniMoon's folder on the iPhone: %1").arg(output);
    return {};
}

void PhoneSyncController::unmount()
{
    const QStorageInfo storage(mountPoint());
    if (!storage.isValid() || QDir(storage.rootPath()) != QDir(mountPoint()))
        return;
    if (!run(u"fusermount3"_s, {u"-u"_s, mountPoint()}))
        run(u"fusermount"_s, {u"-u"_s, mountPoint()});
}

void PhoneSyncController::check(const QString &libraryFolder)
{
    if (m_state == Checking || m_state == Syncing)
        return;
    m_library = libraryFolder;
    m_plan = {};
    m_progress = 0;
    emit progressChanged();
    setState(Checking, tr("Comparing with the iPhone…"));
    m_worker = QtConcurrent::run([this, skip = m_skipDuplicates] {
        const QString error = mount();
        PhoneSync::Plan plan = error.isEmpty() ? PhoneSync::plan(m_library, mountPoint(), skip) : PhoneSync::Plan{};
        QMetaObject::invokeMethod(this, [this, error, plan = std::move(plan)]() mutable {
            if (!error.isEmpty()) {
                setState(Failed, error);
                return;
            }
            m_plan = std::move(plan);
            if (m_plan.copy.isEmpty() && m_plan.remove.isEmpty()) {
                unmount();
                setState(Done, m_plan.duplicates.isEmpty()
                                   ? tr("The iPhone is up to date.")
                                   : tr("The iPhone is up to date (duplicates skipped: %1).").arg(m_plan.duplicates.size()));
            } else {
                setState(Ready);
            }
        });
    });
}

void PhoneSyncController::sync()
{
    if (m_state != Ready)
        return;
    m_cancel = false;
    setState(Syncing, tr("Syncing…"));
    m_worker = QtConcurrent::run([this] {
        const QString error = PhoneSync::apply(
            m_plan, m_library, mountPoint(),
            [this](int done, int total) {
                QMetaObject::invokeMethod(this, [this, done, total] {
                    m_progress = total > 0 ? qreal(done) / total : 1;
                    emit progressChanged();
                });
            },
            m_cancel);
        unmount();
        QMetaObject::invokeMethod(this, [this, error] {
            if (!error.isEmpty())
                setState(Failed, error);
            else if (m_cancel)
                setState(Idle, tr("Stopped. Restart MiniMoon to see what was copied."));
            else
                setState(Done, tr("Done. Restart MiniMoon on the iPhone to see the changes."));
        });
    });
}

void PhoneSyncController::cancel()
{
    if (m_state == Syncing) {
        m_cancel = true;
        return;
    }
    if (m_state == Ready) {
        unmount();
        m_plan = {};
        setState(Idle);
    }
}

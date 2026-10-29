#pragma once

#include "core/PhoneSync.h"

#include <QFuture>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <atomic>

// Settings → iPhone sync: copies the library's songs into MiniMoon's folder on an iPhone over USB
// (ifuse from libimobiledevice mounts the app's Documents), mirroring deletions. The app lists
// new songs after it is restarted. Linux only for now.
class PhoneSyncController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged FINAL)
    Q_PROPERTY(int copyCount READ copyCount NOTIFY stateChanged FINAL)
    Q_PROPERTY(int removeCount READ removeCount NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 copyBytes READ copyBytes NOTIFY stateChanged FINAL)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged FINAL)
    Q_PROPERTY(bool supported READ supported CONSTANT FINAL)
    // The section is hidden until a code is typed anywhere in the app, until grim is closed; only
    // its hash is in the source.
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY unlockedChanged FINAL)
    // The plan from check(), as relative paths.
    Q_PROPERTY(QStringList added READ added NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList updated READ updated NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList removed READ removed NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList duplicates READ duplicates NOTIFY stateChanged FINAL)
    // Don't copy a song already on the iPhone in another folder (or twice in the library).
    // Remembered as sync/skipDuplicates; takes effect on the next check().
    Q_PROPERTY(bool skipDuplicates READ skipDuplicates WRITE setSkipDuplicates NOTIFY skipDuplicatesChanged FINAL)

public:
    enum State { Idle, Checking, Ready, Syncing, Done, Failed };
    Q_ENUM(State)

    explicit PhoneSyncController(QObject *parent = nullptr);
    ~PhoneSyncController() override;

    State state() const;
    QString message() const;
    int copyCount() const;
    int removeCount() const;
    qint64 copyBytes() const;
    qreal progress() const;
    bool supported() const;
    bool unlocked() const;
    QStringList added() const;
    QStringList updated() const;
    QStringList removed() const;
    QStringList duplicates() const;
    bool skipDuplicates() const;
    void setSkipDuplicates(bool skip);

    // Connects to the phone and compares; ends in Ready (plan shown, phone still mounted) or Failed.
    Q_INVOKABLE void check(const QString &libraryFolder);
    // Applies the plan from check(); ends in Done or Failed, the phone unmounted.
    Q_INVOKABLE void sync();
    // Drops a plan (Ready) or stops a running sync after the current file.
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void progressChanged();
    void skipDuplicatesChanged();
    void unlockedChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setState(State state, const QString &message = {});
    QString mountPoint() const;
    QString mount();   // empty on success, else a message (worker thread)
    void unmount();

    State m_state = Idle;
    QString m_message;
    QString m_library;
    PhoneSync::Plan m_plan;
    qreal m_progress = 0;
    std::atomic_bool m_cancel{false};
    QFuture<void> m_worker; // waited for on destruction: it uses this object
    bool m_skipDuplicates = false;
    bool m_unlocked = false;
    QString m_typed; // the last digits typed
};

#pragma once

#include "core/AudioConverter.h"
#include "core/SlskdClient.h"
#include "core/SlskdRunner.h"
#include "providers/DownloadsModel.h"
#include "providers/SearchGroupsModel.h"
#include "providers/SearchResultsModel.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;
class QTimer;
class SettingsController;

// Soulseek page backend: slskd connection settings, status, search, downloads,
// and the download pipeline: finished downloads are converted to AAC 320 kbps
// .m4a, the original goes to the Recycle Bin, and the result moves into the
// library folder. One instance, created in main.cpp before the QML engine.
class SlskdController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // "Run slskd for me": grim. downloads and runs its own slskd (SlskdRunner) and logs it in as `username`.
    // Off: the user's own slskd at serverUrl/apiKey.
    Q_PROPERTY(bool managed READ managed NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString username READ username NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString managedStatus READ managedStatus NOTIFY managedStatusChanged FINAL)
    Q_PROPERTY(bool managedBusy READ managedBusy NOTIFY managedStatusChanged FINAL) // downloading slskd
    Q_PROPERTY(bool managedFailed READ managedFailed NOTIFY managedStatusChanged FINAL)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString apiKey READ apiKey NOTIFY settingsChanged FINAL)
    // What finished downloads become: "original" (moved as downloaded, no ffmpeg needed) or an
    // AudioConverter format ("m4a", "mp3", "flac"). Default "m4a".
    Q_PROPERTY(QString downloadFormat READ downloadFormat WRITE setDownloadFormat NOTIFY downloadFormatChanged FINAL)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged FINAL)
    Q_PROPERTY(int responseCount READ responseCount NOTIFY searchingChanged FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged FINAL)
    Q_PROPERTY(SearchResultsModel *results READ results CONSTANT FINAL)
    Q_PROPERTY(SearchGroupsModel *groups READ groups CONSTANT FINAL)
    Q_PROPERTY(DownloadsModel *downloads READ downloads CONSTANT FINAL)

public:
    enum Status {
        Checking, // no answer yet since start or a settings change
        Offline,
        AuthRequired,
        NotLoggedIn,
        Online,
    };
    Q_ENUM(Status)

    explicit SlskdController(SettingsController &settings, QObject *parent = nullptr);
    ~SlskdController() override;

    static SlskdController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    bool managed() const;
    QString username() const;
    QString managedStatus() const;
    bool managedBusy() const;
    bool managedFailed() const;
    QString serverUrl() const;
    QString apiKey() const;
    QString downloadFormat() const;
    void setDownloadFormat(const QString &format);
    Status status() const;
    bool searching() const;
    int responseCount() const;
    QString errorText() const;
    SearchResultsModel *results() const;
    SearchGroupsModel *groups() const;
    DownloadsModel *downloads() const;

    // Saves to QSettings. The key is optional and stored in plain text in the user's registry.
    Q_INVOKABLE void saveSettings(const QString &serverUrl, const QString &apiKey);
    // Switches to grim.'s own slskd: stores the login (the password in the system's credential store),
    // downloads slskd if needed and starts it. An empty password keeps the stored one.
    Q_INVOKABLE void useManaged(const QString &username, const QString &password);
    // Back to the user's own slskd; grim.'s copy stops.
    Q_INVOKABLE void useOwn();
    // Starts grim.'s slskd again after it failed (or retries the download).
    Q_INVOKABLE void restartManaged();
    Q_INVOKABLE void checkStatus();
    // Shows "Checking" and asks slskd again (the title bar status was clicked, settings changed).
    Q_INVOKABLE void recheckStatus();
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void download(int resultRow);
    // Every file of an album from groups(), in one request.
    Q_INVOKABLE void downloadAlbum(int albumIndex);
    Q_INVOKABLE void downloadAlbumFile(int albumIndex, int fileIndex);
    Q_INVOKABLE void refreshDownloads();
    // Removes finished transfers from the Downloads list (in slskd too); active ones stay.
    Q_INVOKABLE void clearFinishedDownloads();

signals:
    void settingsChanged();
    void managedStatusChanged();
    void statusChanged();
    void downloadFormatChanged();
    void searchingChanged();
    void errorTextChanged();
    // A converted download now sits in the library folder at `path`.
    void trackImported(const QString &path);

private:
    void pollSearch();
    void finishSearch();
    void setErrorText(const QString &text);
    void setStatus(Status status);
    // Points the client at grim.'s slskd or the user's, and starts or stops grim.'s copy to match.
    void applyServer();
    // Polls the status every kStatusPollMs up to `checks` times (slskd may still be starting).
    void pollStatus(int checks);
    // Queues every newly succeeded, not-yet-handled download for conversion.
    void convertFinished(const QList<SlskdDownload> &downloads);
    void importToLibrary(const QString &path, const QString &downloadId, const QString &doneNote);

    static inline SlskdController *s_instance = nullptr;
    SettingsController &m_settings;

    SlskdClient *m_client;
    SearchResultsModel *m_results;
    SearchGroupsModel *m_groups;
    DownloadsModel *m_downloads;
    QTimer *m_searchTimer;
    QTimer *m_downloadsTimer;
    QTimer *m_statusTimer;
    int m_statusChecksLeft = 0;
    SlskdRunner *m_runner;
    bool m_managed = false;
    QString m_username;
    QString m_managedApiKey; // random, accepted only from 127.0.0.1
    AudioConverter *m_converter;
    Status m_status = Checking;

    QString m_downloadsDirectory;           // local folder slskd moves finished files to
    QSet<QString> m_handledDownloads;       // download ids already sent to (or skipped by) conversion
    QHash<QString, QString> m_conversions;  // source path -> download id

    QString m_downloadFormat;
    QString m_serverUrl;
    QString m_apiKey;
    QString m_searchId; // current search; replies for older searches are dropped
    bool m_searching = false;
    int m_responseCount = 0;
    QString m_errorText;
};

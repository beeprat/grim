#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QNetworkAccessManager;
class QQmlEngine;

// Asks GitHub once at startup whether a newer grim. release exists; the title bar then shows a note.
// Clicking it (install()) downloads that release's installer, runs it silently and quits; the installer
// waits for grim. to close, replaces it and starts the new version. Copies not installed by the installer
// (the 7z, Linux) open the download page instead. Failures of the check stay silent. One instance,
// created in main.cpp and handed to QML through create().
class UpdateController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Empty while no newer version is known.
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateFound FINAL)
    Q_PROPERTY(QUrl downloadUrl READ downloadUrl NOTIFY updateFound FINAL)
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged FINAL) // 0..1 while Downloading

public:
    enum State { Available, Downloading, Installing, Failed };
    Q_ENUM(State)

    explicit UpdateController(const QString &currentVersion, QObject *parent = nullptr);
    ~UpdateController() override;

    static UpdateController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    QString latestVersion() const;
    QUrl downloadUrl() const;
    State state() const;
    double progress() const;

    void check();
    Q_INVOKABLE void install();

signals:
    void updateFound();
    void stateChanged();

private:
    void setState(State state);
    bool installedByInstaller() const;

    QString m_currentVersion;
    QString m_latestVersion;
    QUrl m_downloadUrl;
    QUrl m_installerUrl;
    State m_state = Available;
    double m_progress = 0;
    QNetworkAccessManager *m_network;
};

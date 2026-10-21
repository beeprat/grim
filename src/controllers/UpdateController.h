#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QNetworkAccessManager;
class QQmlEngine;

// Asks GitHub once at startup whether a newer grim. release exists; the title bar then shows a note
// that opens its download page. Failures (offline, rate limit) stay silent. One instance, created in
// main.cpp and handed to QML through create().
class UpdateController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Empty while no newer version is known.
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateFound FINAL)
    Q_PROPERTY(QUrl downloadUrl READ downloadUrl NOTIFY updateFound FINAL)

public:
    explicit UpdateController(const QString &currentVersion, QObject *parent = nullptr);
    ~UpdateController() override;

    static UpdateController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    QString latestVersion() const;
    QUrl downloadUrl() const;

    void check();

signals:
    void updateFound();

private:
    QString m_currentVersion;
    QString m_latestVersion;
    QUrl m_downloadUrl;
    QNetworkAccessManager *m_network;
};

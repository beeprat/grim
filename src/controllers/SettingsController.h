#pragma once

#include "core/StringsTranslator.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

// App preferences shown on the Settings page, stored in QSettings.
// One instance, created in main.cpp before the QML engine.
class SettingsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString libraryFolder READ libraryFolder NOTIFY libraryFolderChanged FINAL)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY minimizeToTrayChanged FINAL)
    // Translation file name without ".txt"; empty = English (the texts in the code).
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    // Translation files found in translationsFolder.
    Q_PROPERTY(QStringList languages READ languages NOTIFY languagesChanged FINAL)
    Q_PROPERTY(QString translationsFolder READ translationsFolder CONSTANT FINAL)
    // Look up lyrics on lrclib.net for songs without them (sends artist, title, album, length).
    Q_PROPERTY(bool onlineLyrics READ onlineLyrics WRITE setOnlineLyrics NOTIFY onlineLyricsChanged FINAL)
    // Seconds the end of a track overlaps the next one; 0 = off.
    Q_PROPERTY(double crossfadeSeconds READ crossfadeSeconds WRITE setCrossfadeSeconds NOTIFY crossfadeSecondsChanged FINAL)
    // Disk space used by album cover thumbnails, artist photos and online lyrics, in bytes. Re-read on
    // refreshCacheSize() and after clearCache().
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY cacheChanged FINAL)

public:
    // No default argument on purpose: a default-constructible QML_SINGLETON is built by the
    // QML engine itself instead of through create(), giving a second instance.
    explicit SettingsController(QObject *parent);
    ~SettingsController() override;

    static SettingsController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // Where the library is scanned from and converted downloads are moved to.
    // Defaults to the user's Music folder.
    QString libraryFolder() const;
    // Takes the file:// URL a QML FolderDialog returns.
    Q_INVOKABLE void setLibraryFolder(const QUrl &folderUrl);

    bool minimizeToTray() const;
    void setMinimizeToTray(bool enabled);

    bool onlineLyrics() const;
    void setOnlineLyrics(bool enabled);

    // 0 (off) to 12 s, in tenths of a second.
    double crossfadeSeconds() const;
    void setCrossfadeSeconds(double seconds);

    QString language() const;
    void setLanguage(const QString &language);
    QStringList languages() const;
    QString translationsFolder() const;

    // Writes "<name>.txt" with every text of the app, ready to translate, and opens it.
    // An existing file is updated: its translations are kept and new texts are added.
    // Returns the file path, or empty if the name is unusable.
    Q_INVOKABLE QString createTranslation(const QString &name);
    Q_INVOKABLE void openTranslationsFolder() const;
    // Re-reads the folder (files added or edited by hand) and reloads the current language.
    Q_INVOKABLE void reloadLanguages();

    qint64 cacheBytes() const;
    Q_INVOKABLE void refreshCacheSize();
    // Deletes the cached cover thumbnails, artist photos and online lyrics; they are rebuilt or
    // downloaded again when next needed. Covers and lyrics already in memory stay until restart.
    Q_INVOKABLE void clearCache();

signals:
    void libraryFolderChanged();
    void minimizeToTrayChanged();
    void languageChanged();
    void onlineLyricsChanged();
    void crossfadeSecondsChanged();
    void languagesChanged();
    void cacheChanged();
    // The installed translation changed; main.cpp calls QQmlEngine::retranslate().
    void translationApplied();

private:
    static inline SettingsController *s_instance = nullptr;

    void applyLanguage();

    QString m_libraryFolder;
    bool m_minimizeToTray = false;
    bool m_onlineLyrics = true;
    double m_crossfadeSeconds = 0;
    QString m_language;
    QStringList m_languages;
    StringsTranslator m_translator;
};

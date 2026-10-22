#include "controllers/SettingsController.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJSEngine>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

const auto kLibraryFolderKey = "library/folder"_L1;
const auto kMinimizeToTrayKey = "window/minimizeToTray"_L1;
const auto kLanguageKey = "ui/language"_L1;
const auto kOnlineLyricsKey = "lyrics/fetchOnline"_L1;
const auto kCrossfadeKey = "playback/crossfadeSeconds"_L1;
constexpr double kMaxCrossfadeSeconds = 12;

// Tenths of a second within 0..12: the slider's precision, and what gets stored.
double roundedCrossfade(double seconds)
{
    return std::round(std::clamp(seconds, 0.0, kMaxCrossfadeSeconds) * 10) / 10;
}

// Every qsTr/tr text in the app, collected at build time (cmake/ExtractUiStrings.cmake).
QStringList uiStrings()
{
    QFile file(u":/ui_strings.txt"_s);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QStringList strings;
    for (const QString &line : QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts))
        strings.append(StringsTranslator::unescape(line));
    return strings;
}

} // namespace

SettingsController::SettingsController(QObject *parent)
    : QObject(parent)
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    const QSettings settings;
    m_libraryFolder = settings.value(kLibraryFolderKey,
                                     QStandardPaths::writableLocation(QStandardPaths::MusicLocation)).toString();
    m_minimizeToTray = settings.value(kMinimizeToTrayKey, false).toBool();
    m_language = settings.value(kLanguageKey).toString();
    m_onlineLyrics = settings.value(kOnlineLyricsKey, true).toBool();
    m_crossfadeSeconds = roundedCrossfade(settings.value(kCrossfadeKey, 0).toDouble()); // older versions stored whole seconds

    QDir().mkpath(translationsFolder());
    reloadLanguages();
}

SettingsController::~SettingsController()
{
    QCoreApplication::removeTranslator(&m_translator);
    s_instance = nullptr;
}

SettingsController *SettingsController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "SettingsController::create", "construct SettingsController before loading QML");
    // Owned by main.cpp; stop the QML engine from deleting it.
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString SettingsController::libraryFolder() const
{
    return m_libraryFolder;
}

namespace {

// CoverCache (covers), ArtistImageFetcher (artists) and LyricsFetcher (lyrics) folders, as created in
// main.cpp, StatsController and PlayerController.
QStringList cacheFolders()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return {base + QStringLiteral("/covers"), base + QStringLiteral("/artists"), base + QStringLiteral("/lyrics")};
}

} // namespace

qint64 SettingsController::cacheBytes() const
{
    qint64 bytes = 0;
    for (const QString &folder : cacheFolders()) {
        QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            bytes += it.nextFileInfo().size();
    }
    return bytes;
}

void SettingsController::refreshCacheSize()
{
    emit cacheChanged();
}

void SettingsController::clearCache()
{
    // Files only: the folders stay, since the caches write into them without re-creating them.
    for (const QString &folder : cacheFolders()) {
        QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            QFile::remove(it.next()); // a file being written right now may stay; harmless
    }
    emit cacheChanged();
}

QString SettingsController::importBackground(const QUrl &imageUrl)
{
    const QString source = imageUrl.toLocalFile();
    if (!source.isEmpty() && QImageReader(source).format().isEmpty())
        return {}; // not a picture Qt can read: keep the current one
    const QDir data(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    const QString name = source.isEmpty() ? QString()
                                          : u"background-%1.%2"_s.arg(QDateTime::currentMSecsSinceEpoch())
                                                .arg(QFileInfo(source).suffix().toLower());
    if (!name.isEmpty() && !QFile::copy(source, data.filePath(name)))
        return {}; // the current picture stays
    for (const QString &old : data.entryList({u"background-*"_s}, QDir::Files))
        if (old != name)
            QFile::remove(data.filePath(old));
    return name.isEmpty() ? QString() : QUrl::fromLocalFile(data.filePath(name)).toString();
}

void SettingsController::setLibraryFolder(const QUrl &folderUrl)
{
    const QString folder = QDir::cleanPath(folderUrl.toLocalFile());
    if (folder.isEmpty() || folder == m_libraryFolder)
        return;
    m_libraryFolder = folder;
    QSettings().setValue(kLibraryFolderKey, folder);
    emit libraryFolderChanged();
}

bool SettingsController::minimizeToTray() const
{
    return m_minimizeToTray;
}

bool SettingsController::onlineLyrics() const
{
    return m_onlineLyrics;
}

void SettingsController::setOnlineLyrics(bool enabled)
{
    if (enabled == m_onlineLyrics)
        return;
    m_onlineLyrics = enabled;
    QSettings().setValue(kOnlineLyricsKey, enabled);
    emit onlineLyricsChanged();
}

double SettingsController::crossfadeSeconds() const
{
    return m_crossfadeSeconds;
}

void SettingsController::setCrossfadeSeconds(double seconds)
{
    seconds = roundedCrossfade(seconds);
    if (seconds == m_crossfadeSeconds)
        return;
    m_crossfadeSeconds = seconds;
    QSettings().setValue(kCrossfadeKey, seconds);
    emit crossfadeSecondsChanged();
}

QString SettingsController::language() const
{
    return m_language;
}

void SettingsController::setLanguage(const QString &language)
{
    if (language == m_language)
        return;
    m_language = language;
    QSettings().setValue(kLanguageKey, language);
    applyLanguage();
    emit languageChanged();
}

QStringList SettingsController::languages() const
{
    return m_languages;
}

QString SettingsController::translationsFolder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + u"/translations"_s;
}

void SettingsController::reloadLanguages()
{
    QStringList found;
    for (const QFileInfo &file : QDir(translationsFolder()).entryInfoList({u"*.txt"_s}, QDir::Files, QDir::Name))
        found.append(file.completeBaseName());
    if (found != m_languages) {
        m_languages = found;
        emit languagesChanged();
    }
    applyLanguage();
}

void SettingsController::applyLanguage()
{
    QCoreApplication::removeTranslator(&m_translator);
    m_translator.clear();
    if (!m_language.isEmpty()) {
        QString error;
        if (m_translator.loadFile(QDir(translationsFolder()).filePath(m_language + u".txt"_s), &error))
            QCoreApplication::installTranslator(&m_translator);
        else
            qWarning().noquote() << "SettingsController: cannot load language" << m_language << error;
    }
    emit translationApplied();
}

QString SettingsController::createTranslation(const QString &name)
{
    // The name becomes a file name: drop characters Windows does not allow there.
    static const QRegularExpression forbidden(uR"([<>:"/\\|?*\x00-\x1f])"_s);
    const QString cleanName = QString(name).remove(forbidden).trimmed();
    if (cleanName.isEmpty())
        return {};

    const QString path = QDir(translationsFolder()).filePath(cleanName + u".txt"_s);
    // Updating an existing file keeps its translations; a new file has none.
    const QHash<QString, QString> kept = StringsTranslator::parse([&] {
        QFile old(path);
        return old.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(old.readAll()) : QString();
    }());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning().noquote() << "SettingsController: cannot write" << path << file.errorString();
        return {};
    }
    file.write(StringsTranslator::makeTemplate(uiStrings(), kept).toUtf8());
    file.close();

    reloadLanguages();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    return path;
}

void SettingsController::openTranslationsFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(translationsFolder()));
}

void SettingsController::setMinimizeToTray(bool enabled)
{
    if (enabled == m_minimizeToTray)
        return;
    m_minimizeToTray = enabled;
    QSettings().setValue(kMinimizeToTrayKey, enabled);
    emit minimizeToTrayChanged();
}

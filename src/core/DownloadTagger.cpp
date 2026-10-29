#include "core/DownloadTagger.h"

#include "core/TagReader.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

using namespace Qt::StringLiterals;

namespace DownloadTagger {

namespace {

// An edition note: remaster(ed), deluxe, anniversary, with whatever else the bracket or dash part holds
// ("2009", "Edition", "50th"). An anniversary edition's tracks thus join the original album.
const QString kEdition = u"(?:remaster(?:ed)?|deluxe|anniversary)"_s;

// The value most of `values` have (ignoring empty ones); ties go to the first seen.
template<typename T, typename IsEmpty>
T mostCommon(const QList<T> &values, IsEmpty isEmpty)
{
    QHash<T, int> counts;
    T best{};
    int bestCount = 0;
    for (const T &value : values) {
        if (isEmpty(value))
            continue;
        const int count = ++counts[value];
        if (count > bestCount) {
            best = value;
            bestCount = count;
        }
    }
    return best;
}

// The folder part of a path (library paths use forward slashes).
QString folderOf(const QString &path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    return clean.left(clean.lastIndexOf(u'/'));
}

QString simplified(QString text)
{
    text = cleanTitle(text).toCaseFolded();
    static const QRegularExpression nonWord(u"[^\\w]+"_s, QRegularExpression::UseUnicodePropertiesOption);
    return text.replace(nonWord, u" "_s).simplified();
}

} // namespace

QString cleanTitle(const QString &title)
{
    // "(Remastered 2009)", "[2011 Deluxe Edition]"
    static const QRegularExpression bracket(u"\\s*[\\(\\[][^\\)\\]]*\\b"_s + kEdition + u"\\b[^\\)\\]]*[\\)\\]]"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    // " - Remastered 2009", " - 2015 Remaster", " - Deluxe Edition": a trailing dash part
    static const QRegularExpression dash(u"\\s+[-–—]\\s+[^-–—]*\\b"_s + kEdition + u"\\b[^-–—]*$"_s,
                                         QRegularExpression::CaseInsensitiveOption);
    QString cleaned = title;
    cleaned.remove(bracket);
    cleaned.remove(dash);
    cleaned = cleaned.trimmed();
    return cleaned.isEmpty() ? title : cleaned;
}

QString albumKey(const QString &album)
{
    // Release extras that are not part of the album's name: "(Super Deluxe Edition)", "[Expanded]",
    // "- Bonus Track Version", or the same words trailing without brackets ("Abbey Road Super Deluxe").
    static const QString extra =
        u"(?:remaster(?:ed)?|deluxe|anniversary|expanded|edition|version|bonus|reissue|collector'?s?|legacy|"
        "special|super|mono|stereo)"_s;
    static const QRegularExpression bracket(u"\\s*[\\(\\[][^\\)\\]]*\\b"_s + extra + u"\\b[^\\)\\]]*[\\)\\]]"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression dash(u"\\s+[-–—:]\\s+[^-–—:]*\\b"_s + extra + u"\\b[^-–—:]*$"_s,
                                         QRegularExpression::CaseInsensitiveOption);
    // After simplified(): lower case, words only. Numbers ("50th", "2019") go only along with such a word.
    static const QRegularExpression trailing(u"(?:\\s+(?:\\d+(?:st|nd|rd|th)?|"_s + extra + u"|tracks?|cd|lp))+$"_s);
    static const QRegularExpression extraWord(u"\\b"_s + extra + u"\\b"_s);

    QString name = album;
    name.remove(bracket);
    name.remove(dash);
    QString key = simplified(name.trimmed().isEmpty() ? album : name);
    const QRegularExpressionMatch tail = trailing.match(key);
    if (tail.hasMatch() && tail.capturedStart() > 0 && tail.captured().contains(extraWord))
        key.truncate(tail.capturedStart());
    return key;
}

QString artistKey(const QString &artist)
{
    QString key = simplified(artist);
    if (key.startsWith(u"the "_s))
        key.remove(0, 4);
    return key;
}

QString cleanFileName(const QString &fileName, int trackNumber)
{
    // "01 ", "01. ", "01 - ", "1-01 " (disc-track): the last number is the track.
    static const QRegularExpression number(u"^(?:\\d{1,2}[-.])?(\\d{1,3})(?:\\s*[-–._]\\s*|\\s+)"_s);
    const QRegularExpressionMatch found = number.match(fileName);
    if (!found.hasMatch())
        return fileName;
    // Only a real track number: the tag's, or zero-padded ("07"). "99 Problems.mp3" keeps its 99.
    const QString digits = found.captured(1);
    if (digits.toInt() != trackNumber && !(digits.size() > 1 && digits.startsWith(u'0')))
        return fileName;
    const QString cleaned = fileName.mid(found.capturedLength());
    // Keep the number if nothing but the extension would be left ("01.mp3").
    return cleaned.lastIndexOf(u'.') <= 0 || cleaned.startsWith(u' ') ? fileName : cleaned;
}

AlbumMatch findAlbum(const Track &download, const QList<Track> &library)
{
    const QString album = albumKey(download.album);
    if (album.isEmpty())
        return {};
    const QString artist = artistKey(download.artist);

    // Same album, same artist (if the download names one); then the folder most of them are in.
    QList<const Track *> matches;
    QStringList folders;
    for (const Track &track : library) {
        if (albumKey(track.album) != album || (!artist.isEmpty() && artistKey(track.artist) != artist))
            continue;
        if (QDir::cleanPath(QDir::fromNativeSeparators(track.path)) == QDir::cleanPath(QDir::fromNativeSeparators(download.path)))
            continue; // the download itself, if the library already lists it
        matches.append(&track);
        folders.append(folderOf(track.path));
    }
    const QString folder = mostCommon(folders, [](const QString &value) { return value.isEmpty(); });
    if (folder.isEmpty())
        return {};

    AlbumMatch match;
    match.found = true;
    match.folder = folder;
    QStringList artists;
    QStringList albums;
    QList<int> years;
    for (const Track *track : matches) {
        if (folderOf(track->path) != folder)
            continue;
        match.paths.append(track->path);
        artists.append(track->artist);
        albums.append(track->album);
        years.append(track->year);
    }
    const auto emptyText = [](const QString &value) { return value.isEmpty(); };
    match.artist = mostCommon(artists, emptyText);
    match.album = mostCommon(albums, emptyText);
    match.year = mostCommon(years, [](int year) { return year <= 0; });
    return match;
}

namespace {

// Characters Windows refuses in names (and "/" everywhere) become "_"; trailing dots and spaces go.
QString safeName(QString name)
{
    static const QRegularExpression kBad(u"[\\\\/:*?\"<>|\\x00-\\x1f]"_s);
    name.replace(kBad, u"_"_s);
    while (name.endsWith(u'.') || name.endsWith(u' '))
        name.chop(1);
    return name.trimmed();
}

// The subfolder of `parent` whose name gives `key`, else empty.
QString findFolder(const QDir &parent, const QString &key, QString (*keyOf)(const QString &))
{
    for (const QString &name : parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (keyOf(name) == key)
            return parent.filePath(name);
    }
    return {};
}

// "3. Fourth (1971)" → "Fourth": the library's numbered, dated album folders.
QString albumFolderKey(const QString &folderName)
{
    static const QRegularExpression kNumber(u"^\\d+\\.\\s*"_s);
    static const QRegularExpression kYear(u"\\s*\\(\\d{4}\\)$"_s);
    return albumKey(QString(folderName).remove(kNumber).remove(kYear));
}

} // namespace

QString libraryFolderFor(const QString &library, const QString &artist, const QString &album)
{
    const QString artistName = safeName(artist);
    if (artistName.isEmpty() || library.isEmpty())
        return {};
    const QDir root(library);
    QString artistFolder = findFolder(root, artistKey(artist), &artistKey);
    if (artistFolder.isEmpty())
        artistFolder = root.filePath(artistName);
    const QString albumName = safeName(album);
    if (albumName.isEmpty())
        return artistFolder;
    const QString albumFolder = findFolder(QDir(artistFolder), albumKey(album), &albumFolderKey);
    return albumFolder.isEmpty() ? QDir(artistFolder).filePath(albumName) : albumFolder;
}

QByteArray majorityCover(const QStringList &paths)
{
    QList<QByteArray> covers;
    for (const QString &path : paths)
        covers.append(TagReader::readCover(path));
    return mostCommon(covers, [](const QByteArray &cover) { return cover.isEmpty(); });
}

bool writeTags(const QString &path, const QString &title, const QString &artist, const QString &album, int year,
               const QByteArray &cover)
{
    TagLib::FileRef file(QFileInfo(path).filesystemFilePath().c_str(), false);
    if (file.isNull() || !file.tag())
        return false;
    TagLib::Tag *tag = file.tag();
    const auto text = [](const QString &value) { return TagLib::String(value.toStdString(), TagLib::String::UTF8); };
    if (!title.isEmpty())
        tag->setTitle(text(title));
    if (!artist.isEmpty())
        tag->setArtist(text(artist));
    if (!album.isEmpty())
        tag->setAlbum(text(album));
    if (year > 0)
        tag->setYear(unsigned(year));
    if (!cover.isEmpty()) {
        const bool png = cover.startsWith("\x89PNG");
        file.setComplexProperties("PICTURE", {{
            {"data", TagLib::ByteVector(cover.constData(), unsigned(cover.size()))},
            {"mimeType", TagLib::String(png ? "image/png" : "image/jpeg")},
            {"pictureType", TagLib::String("Front Cover")},
            {"description", TagLib::String()},
        }});
    }
    return file.save();
}

} // namespace DownloadTagger

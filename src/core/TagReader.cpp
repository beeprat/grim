#include "core/TagReader.h"

#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace {

QString toQString(const TagLib::String &value)
{
    return QString::fromStdWString(value.toWString()).trimmed();
}

} // namespace

namespace TagReader {

Track read(const QString &filePath)
{
    const QFileInfo info(filePath);
    Track track;
    track.path = info.absoluteFilePath();

    // filesystem paths are wchar_t on Windows (TagLib::FileName too) and UTF-8 bytes on Linux, so non-ASCII names work.
    const TagLib::FileRef file(info.filesystemAbsoluteFilePath().c_str(), true,
                               TagLib::AudioProperties::Average);
    if (!file.isNull()) {
        if (const TagLib::Tag *tag = file.tag()) {
            track.title = toQString(tag->title());
            track.artist = toQString(tag->artist());
            track.album = toQString(tag->album());
            track.year = static_cast<int>(tag->year());
            track.genre = toQString(tag->genre());
            track.trackNumber = static_cast<int>(tag->track());
        }
        // Disc number is not in the basic Tag interface; "2/3" means disc 2 of 3.
        const TagLib::StringList discs = file.properties()["DISCNUMBER"];
        if (!discs.isEmpty())
            track.discNumber = toQString(discs.front()).section(u'/', 0, 0).toInt();
        if (const TagLib::AudioProperties *properties = file.audioProperties())
            track.durationMs = properties->lengthInMilliseconds();
    }

    if (track.title.isEmpty())
        track.title = info.completeBaseName();
    return track;
}

QByteArray readCover(const QString &filePath)
{
    const std::filesystem::path path = QFileInfo(filePath).filesystemFilePath();
    const TagLib::FileRef file(path.c_str(), false);
    if (!file.isNull()) {
        const auto pictures = file.complexProperties("PICTURE");
        // Prefer the front cover; otherwise take the first picture.
        const TagLib::VariantMap *chosen = nullptr;
        for (const auto &picture : pictures) {
            if (!chosen || picture.value("pictureType").toString() == "Front Cover")
                chosen = &picture;
        }
        if (chosen) {
            const TagLib::ByteVector data = chosen->value("data").toByteVector();
            if (!data.isEmpty())
                return QByteArray(data.data(), static_cast<qsizetype>(data.size()));
        }
    }

    const QDir dir = QFileInfo(filePath).dir();
    for (const QString &name : dir.entryList({u"cover.*"_s, u"folder.*"_s, u"front.*"_s}, QDir::Files)) {
        const QString suffix = QFileInfo(name).suffix().toLower();
        if (suffix != u"jpg"_s && suffix != u"jpeg"_s && suffix != u"png"_s)
            continue;
        QFile image(dir.filePath(name));
        if (image.open(QIODevice::ReadOnly))
            return image.readAll();
    }
    return {};
}

} // namespace TagReader

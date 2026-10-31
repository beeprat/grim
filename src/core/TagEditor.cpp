#include "core/TagEditor.h"

#include <fileref.h>
#include <tpropertymap.h>

#include <QCoreApplication>
#include <QFileInfo>

#include <string_view>

using namespace Qt::StringLiterals;

namespace {

// Field key → TagLib property name.
struct Field
{
    const char *key;
    const char *property;
};
constexpr Field kFields[] = {
    {TagEditor::kTitle, "TITLE"},     {TagEditor::kArtist, "ARTIST"}, {TagEditor::kAlbumArtist, "ALBUMARTIST"},
    {TagEditor::kAlbum, "ALBUM"},     {TagEditor::kGenre, "GENRE"},   {TagEditor::kYear, "DATE"},
    {TagEditor::kTrack, "TRACKNUMBER"}, {TagEditor::kDisc, "DISCNUMBER"}, {TagEditor::kComposer, "COMPOSER"}, {TagEditor::kLabel, "LABEL"},
};

TagLib::String toTagLib(const QString &value)
{
    return TagLib::String(value.toStdString(), TagLib::String::UTF8);
}

QString toQString(const TagLib::String &value)
{
    return QString::fromStdString(value.to8Bit(true));
}

} // namespace

QVariantMap TagEditor::read(const QString &path)
{
    TagLib::FileRef file(QFileInfo(path).filesystemFilePath().c_str(), false);
    if (file.isNull())
        return {};
    const TagLib::PropertyMap properties = file.properties();
    QVariantMap result;
    for (const Field &field : kFields) {
        const auto it = properties.find(field.property);
        QString value = it == properties.end() || it->second.isEmpty() ? QString() : toQString(it->second.front());
        if (std::string_view(field.key) == kTrack || std::string_view(field.key) == kDisc)
            value = value.section(u'/', 0, 0); // "3/12" → "3"
        result.insert(QLatin1StringView(field.key), value);
    }
    result.insert(u"hasCover"_s, !file.complexProperties("PICTURE").isEmpty());
    return result;
}

QString TagEditor::write(const QString &path, const QVariantMap &fields, Cover cover, const QByteArray &coverData)
{
    const QString name = QFileInfo(path).fileName();
    TagLib::FileRef file(QFileInfo(path).filesystemFilePath().c_str(), false);
    if (file.isNull())
        return QCoreApplication::translate("TagEditor", "Could not open %1.").arg(name);
    TagLib::PropertyMap properties = file.properties();
    for (const Field &field : kFields) {
        const QString key = QLatin1StringView(field.key);
        if (!fields.contains(key))
            continue;
        const QString value = fields.value(key).toString().trimmed();
        if (value.isEmpty())
            properties.erase(field.property);
        else
            properties.replace(field.property, TagLib::StringList(toTagLib(value)));
    }
    file.setProperties(properties);
    if (cover == Cover::Remove) {
        file.setComplexProperties("PICTURE", {});
    } else if (cover == Cover::Replace && !coverData.isEmpty()) {
        const bool png = coverData.startsWith("\x89PNG");
        file.setComplexProperties("PICTURE", {{
            {"data", TagLib::ByteVector(coverData.constData(), unsigned(coverData.size()))},
            {"mimeType", TagLib::String(png ? "image/png" : "image/jpeg")},
            {"pictureType", TagLib::String("Front Cover")},
            {"description", TagLib::String()},
        }});
    }
    if (!file.save())
        return QCoreApplication::translate("TagEditor", "Could not save %1 (read-only, or in use?).").arg(name);
    return {};
}

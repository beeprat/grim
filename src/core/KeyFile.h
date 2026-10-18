#pragma once

#include <QHash>
#include <QString>

// Small hand-editable settings file: one `name = value` per line; blank lines and lines
// starting with # are ignored. Names are case-insensitive (returned lower case), values trimmed.
namespace KeyFile {

QHash<QString, QString> parse(const QString &text);
// Empty if the file is missing or unreadable.
QHash<QString, QString> read(const QString &path);

} // namespace KeyFile

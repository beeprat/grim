#include "core/KeyFile.h"

#include <QFile>

namespace KeyFile {

QHash<QString, QString> parse(const QString &text)
{
    QHash<QString, QString> values;
    for (const QString &rawLine : text.split(u'\n')) {
        const QString line = rawLine.trimmed();
        const qsizetype equals = line.indexOf(u'=');
        if (line.isEmpty() || line.startsWith(u'#') || equals <= 0)
            continue;
        values.insert(line.left(equals).trimmed().toLower(), line.mid(equals + 1).trimmed());
    }
    return values;
}

QHash<QString, QString> read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return parse(QString::fromUtf8(file.readAll()));
}

} // namespace KeyFile

#include "core/StringsTranslator.h"

#include <QFile>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

bool StringsTranslator::loadFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    m_strings = parse(QString::fromUtf8(file.readAll()));
    return true;
}

void StringsTranslator::clear()
{
    m_strings.clear();
}

QString StringsTranslator::translate(const char *, const char *sourceText, const char *, int) const
{
    // Context-free on purpose: one line per English text, whichever file it appears in.
    // An empty result makes Qt fall back to the English text.
    return m_strings.value(QString::fromUtf8(sourceText));
}

bool StringsTranslator::isEmpty() const
{
    return m_strings.isEmpty();
}

QString StringsTranslator::escape(const QString &text)
{
    QString escaped = text;
    escaped.replace(u"\\"_s, u"\\\\"_s).replace(u"\""_s, u"\\\""_s).replace(u"\n"_s, u"\\n"_s);
    return escaped;
}

QString StringsTranslator::unescape(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == u'\\' && i + 1 < text.size()) {
            const QChar next = text.at(++i);
            result.append(next == u'n' ? u'\n' : next);
        } else {
            result.append(text.at(i));
        }
    }
    return result;
}

QHash<QString, QString> StringsTranslator::parse(const QString &text)
{
    // Custom raw-string delimiter: the pattern itself contains )" which would end R"(...)".
    static const QRegularExpression line(uR"re(^\s*"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)"\s*;?\s*$)re"_s);
    QHash<QString, QString> strings;
    for (const QString &row : text.split(u'\n')) {
        const auto match = line.match(row.trimmed());
        if (!match.hasMatch())
            continue; // comments, blank lines, typos
        const QString translated = unescape(match.captured(2));
        if (!translated.isEmpty())
            strings.insert(unescape(match.captured(1)), translated);
    }
    return strings;
}

QString StringsTranslator::makeTemplate(const QStringList &sourceStrings, const QHash<QString, QString> &existing)
{
    QString text = uR"(// GRIM translation file (UTF-8).
// Each line: "English text" = "Your translation";
// Keep the left side exactly as it is. Keep placeholders like %1 and %2 in the translation.
// Lines you leave as they are stay in English. Save, then pick the language in Settings.

)"_s;
    for (const QString &source : sourceStrings) {
        const QString translated = existing.value(source, source);
        text += u"\"%1\" = \"%2\";\n"_s.arg(escape(source), escape(translated));
    }
    return text;
}

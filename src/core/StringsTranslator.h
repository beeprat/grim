#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QTranslator>

// Translations from a plain text file, one string per line:
//
//     "Settings" = "Podesavanja";
//
// Keys are the English texts exactly as written in the code (qsTr/tr). Backslash
// escapes \" \\ \n work inside quotes; the trailing ';' is optional; lines starting
// with // or # are comments. Strings missing from the file stay in English.
class StringsTranslator : public QTranslator
{
public:
    // Replaces the loaded strings. False (and *error set) if the file cannot be read.
    bool loadFile(const QString &path, QString *error = nullptr);
    void clear();

    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
                      int n = -1) const override;
    bool isEmpty() const override;

    // Parses file text into English -> translated. Malformed lines are skipped.
    static QHash<QString, QString> parse(const QString &text);
    // A complete file for every source string: existing translations are kept, the rest
    // default to the English text so the file works before anything is translated.
    static QString makeTemplate(const QStringList &sourceStrings, const QHash<QString, QString> &existing = {});
    // \" \\ \n escapes, as used in the file (and in the source code the strings come from).
    static QString escape(const QString &text);
    static QString unescape(const QString &text);

private:
    QHash<QString, QString> m_strings;
};

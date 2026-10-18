#include "core/StringsTranslator.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestStringsTranslator : public QObject
{
    Q_OBJECT

private slots:
    void parsesLines()
    {
        // Not a raw string: moc's preprocessor reads a "#..." line inside one as a directive.
        const QStringList lines{
            u"// comment"_s,
            u"# also a comment"_s,
            u"\"Settings\" = \"Podesavanja\";"_s,
            u"  \"Library\"=\"Biblioteka\""_s,
            u"\"Say \\\"hi\\\"\" = \"Reci \\\"zdravo\\\"\";"_s,
            u"\"%1 tracks\" = \"%1 pjesama\";"_s,
            u"\"Untranslated\" = \"\";"_s,
            u"not a line"_s,
            u"\"Two\\nlines\" = \"Dvije\\nlinije\";"_s,
        };
        const auto strings = StringsTranslator::parse(lines.join(u'\n'));
        QCOMPARE(strings.value(u"Settings"_s), u"Podesavanja"_s);
        QCOMPARE(strings.value(u"Library"_s), u"Biblioteka"_s);
        QCOMPARE(strings.value(u"Say \"hi\""_s), u"Reci \"zdravo\""_s);
        QCOMPARE(strings.value(u"%1 tracks"_s), u"%1 pjesama"_s);
        QCOMPARE(strings.value(u"Two\nlines"_s), u"Dvije\nlinije"_s);
        QVERIFY(!strings.contains(u"Untranslated"_s)); // empty means "keep English"
        QCOMPARE(strings.size(), 5);
    }

    void templateRoundTrips()
    {
        const QStringList sources{u"Settings"_s, u"Say \"hi\""_s, u"Back\\slash"_s};
        const QString text = StringsTranslator::makeTemplate(sources, {{u"Settings"_s, u"Podesavanja"_s}});
        QVERIFY(text.contains(u"\"Settings\" = \"Podesavanja\";"_s));
        QVERIFY(text.contains(u"\"Say \\\"hi\\\"\" = \"Say \\\"hi\\\"\";"_s));

        const auto parsed = StringsTranslator::parse(text);
        QCOMPARE(parsed.value(u"Settings"_s), u"Podesavanja"_s);
        QCOMPARE(parsed.value(u"Say \"hi\""_s), u"Say \"hi\""_s);
        QCOMPARE(parsed.value(u"Back\\slash"_s), u"Back\\slash"_s);
    }

    void translatesThroughQt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"Bosanski.txt"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(u"\"Settings\" = \"Podešavanja\";\n"_s.toUtf8());
        file.close();

        StringsTranslator translator;
        QVERIFY(translator.loadFile(path));
        QVERIFY(!translator.isEmpty());
        QCoreApplication::installTranslator(&translator);
        QCOMPARE(QCoreApplication::translate("AnyContext", "Settings"), u"Podešavanja"_s);
        QCOMPARE(QCoreApplication::translate("AnyContext", "Library"), u"Library"_s); // falls back
        QCoreApplication::removeTranslator(&translator);
        QCOMPARE(QCoreApplication::translate("AnyContext", "Settings"), u"Settings"_s);

        QVERIFY(!translator.loadFile(dir.filePath(u"missing.txt"_s)));
    }
};

QTEST_GUILESS_MAIN(TestStringsTranslator)
#include "tst_stringstranslator.moc"

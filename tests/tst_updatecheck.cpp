#include "core/UpdateCheck.h"

#include <QTest>

using namespace Qt::StringLiterals;

// Normal string literals only: JSON in raw strings here confuses moc (see CLAUDE.md).
class TestUpdateCheck : public QObject
{
    Q_OBJECT

private slots:
    void comparesVersions()
    {
        QVERIFY(UpdateCheck::isNewer(u"0.1.2"_s, u"0.1.1"_s));
        QVERIFY(UpdateCheck::isNewer(u"0.1.10"_s, u"0.1.9"_s)); // numbers, not text
        QVERIFY(UpdateCheck::isNewer(u"1.0"_s, u"0.9.9"_s));
        QVERIFY(!UpdateCheck::isNewer(u"0.1.1"_s, u"0.1.1"_s));
        QVERIFY(!UpdateCheck::isNewer(u"0.1.0"_s, u"0.1.1"_s));
        QVERIFY(!UpdateCheck::isNewer(u"latest"_s, u"0.1.1"_s));
    }

    void parsesLatestRelease()
    {
        const auto release = UpdateCheck::parse("{\"tag_name\":\"v0.1.2\","
                                                "\"html_url\":\"https://github.com/beeprat/grim/releases/tag/v0.1.2\","
                                                "\"draft\":false,\"prerelease\":false}");
        QVERIFY(release);
        QCOMPARE(release->version, u"0.1.2"_s);
        QCOMPARE(release->pageUrl, QUrl(u"https://github.com/beeprat/grim/releases/tag/v0.1.2"_s));
        QVERIFY(release->installerUrl.isEmpty()); // no assets listed
    }

    void takesInstallerOnlyFromThisRepository()
    {
        const auto release = UpdateCheck::parse(
            "{\"tag_name\":\"v0.1.3\",\"html_url\":\"https://github.com/beeprat/grim/releases/tag/v0.1.3\","
            "\"assets\":[{\"name\":\"GRIM-win64.7z\",\"browser_download_url\":\"https://github.com/beeprat/grim/releases/download/v0.1.3/GRIM-win64.7z\"},"
            "{\"name\":\"grim-setup.exe\",\"browser_download_url\":\"https://github.com/beeprat/grim/releases/download/v0.1.3/grim-setup.exe\"}]}");
        QVERIFY(release);
        QCOMPARE(release->installerUrl, QUrl(u"https://github.com/beeprat/grim/releases/download/v0.1.3/grim-setup.exe"_s));

        const auto elsewhere = UpdateCheck::parse(
            "{\"tag_name\":\"v0.1.3\",\"html_url\":\"https://github.com/beeprat/grim/releases/tag/v0.1.3\","
            "\"assets\":[{\"name\":\"grim-setup.exe\",\"browser_download_url\":\"https://example.com/grim-setup.exe\"}]}");
        QVERIFY(elsewhere);
        QVERIFY(elsewhere->installerUrl.isEmpty());
    }

    void rejectsOddAnswers()
    {
        QVERIFY(!UpdateCheck::parse("not json"));
        QVERIFY(!UpdateCheck::parse("{\"message\":\"API rate limit exceeded\"}"));
        QVERIFY(!UpdateCheck::parse("{\"tag_name\":\"v9\",\"html_url\":\"https://github.com/x\",\"prerelease\":true}"));
        // Opened in the browser, so only GitHub pages over https.
        QVERIFY(!UpdateCheck::parse("{\"tag_name\":\"v9\",\"html_url\":\"https://example.com/grim\"}"));
        QVERIFY(!UpdateCheck::parse("{\"tag_name\":\"v9\",\"html_url\":\"http://github.com/beeprat/grim\"}"));
    }
};

QTEST_GUILESS_MAIN(TestUpdateCheck)
#include "tst_updatecheck.moc"

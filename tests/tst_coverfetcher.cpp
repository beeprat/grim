#include "core/CoverFetcher.h"

#include <QImage>
#include <QTest>

class TestCoverFetcher : public QObject
{
    Q_OBJECT

private slots:
    // Needs the internet; skipped offline.
    void findsLiveCover()
    {
        CoverFetcher fetcher;
        QByteArray cover;
        bool done = false;
        fetcher.fetch(u"Soft Machine"_qs, u"Bundles"_qs, [&](const QByteArray &bytes) { cover = bytes; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        if (cover.isEmpty())
            QSKIP("Deezer not reachable");
        QVERIFY(!QImage::fromData(cover).isNull());
    }
};

QTEST_GUILESS_MAIN(TestCoverFetcher)
#include "tst_coverfetcher.moc"

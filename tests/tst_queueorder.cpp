#include "core/QueueOrder.h"

#include <QTest>

using namespace Qt::StringLiterals;

class TestQueueOrder : public QObject
{
    Q_OBJECT

private slots:
    void dropsShuffledTail()
    {
        // Shuffled: play order c, a, d, b, e; playing a (position 1). Clearing drops d, b, e.
        QList<QString> queue{u"a"_s, u"b"_s, u"c"_s, u"d"_s, u"e"_s};
        QList<int> order{2, 0, 3, 1, 4};
        QueueOrder::dropFrom(order, queue, 2);
        QCOMPARE(queue, (QList<QString>{u"a"_s, u"c"_s}));
        QCOMPARE(order, (QList<int>{1, 0}));
        // Every kept index still names the same track.
        QCOMPARE(queue.at(order.at(0)), u"c"_s);
        QCOMPARE(queue.at(order.at(1)), u"a"_s);
    }

    void outOfRangeChangesNothing()
    {
        QList<QString> queue{u"a"_s, u"b"_s};
        QList<int> order{1, 0};
        QueueOrder::dropFrom(order, queue, 2);
        QueueOrder::dropFrom(order, queue, -1);
        QCOMPARE(queue.size(), 2);
        QCOMPARE(order, (QList<int>{1, 0}));
    }
};

QTEST_GUILESS_MAIN(TestQueueOrder)
#include "tst_queueorder.moc"

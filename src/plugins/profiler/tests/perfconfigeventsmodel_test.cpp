// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "perfconfigeventsmodel_test.h"

#include "../perfconfigeventsmodel.h"
#include "../perfsettings.h"

#include <utils/aspectpresentation.h>

#include <QTest>

namespace Profiler::Internal {

// The events are stored as perf's own strings; the model turns them into cells
// and back. It also answers what each cell offers, which depends on the row's
// event type - a hardware event has no operation, a cache event has three.
class PerfConfigEventsModelTest : public QObject
{
    Q_OBJECT

private slots:
    void testColumnHeaders();
    void testHardwareEventOffersItsCounters();
    void testCacheEventOffersOperationAndResult();
    void testWrittenOutEventsGetAPatternInstead();
    void testChoosingWritesTheEventBack();
    void testWrittenTextRoundTripsThroughTheModel();
};

// The choices as a view would read them off the cell.
static QStringList displays(const QAbstractItemModel &model, int row, int column)
{
    QStringList result;
    const QVariant cell = model.index(row, column).data(Utils::AspectTable::ChoicesRole);
    for (const QVariant &choice : cell.toList())
        result << choice.toMap().value("display").toString();
    return result;
}

static QVariant idOf(const QAbstractItemModel &model, int row, int column,
                     const QString &display)
{
    const QVariant cell = model.index(row, column).data(Utils::AspectTable::ChoicesRole);
    for (const QVariant &choice : cell.toList()) {
        const QVariantMap map = choice.toMap();
        if (map.value("display").toString() == display)
            return map.value("id");
    }
    return {};
}

static bool editable(const QAbstractItemModel &model, int row, int column)
{
    return model.index(row, column).data(Utils::AspectTable::EditableRole).toBool();
}

void PerfConfigEventsModelTest::testColumnHeaders()
{
    PerfSettings settings;
    PerfConfigEventsModel model(&settings);

    QCOMPARE(model.columnCount({}), int(PerfConfigEventsModel::ColumnInvalid));
    const auto header = [&model](int column) {
        return model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
    };
    QCOMPARE(header(PerfConfigEventsModel::ColumnEventType), QString("Event Type"));
    QCOMPARE(header(PerfConfigEventsModel::ColumnResult), QString("Result"));
}

void PerfConfigEventsModelTest::testHardwareEventOffersItsCounters()
{
    PerfSettings settings;
    settings.events.setValue({"cpu-cycles"});
    PerfConfigEventsModel model(&settings);

    QCOMPARE(model.rowCount({}), 1);
    QCOMPARE(model.index(0, PerfConfigEventsModel::ColumnEventType).data().toString(),
             QString("hardware"));
    QCOMPARE(model.index(0, PerfConfigEventsModel::ColumnSubType).data().toString(),
             QString("cpu-cycles"));

    // Every event type can be picked, and the counters are the hardware ones.
    QCOMPARE(displays(model, 0, PerfConfigEventsModel::ColumnEventType),
             QStringList({"hardware", "software", "cache", "raw", "breakpoint", "custom"}));
    const QStringList counters = displays(model, 0, PerfConfigEventsModel::ColumnSubType);
    QCOMPARE(counters.first(), QString("cpu-cycles"));
    QCOMPARE(counters.size(), 10);
    QVERIFY(!counters.contains("L1-dcache"));

    // A hardware event has neither, so the cells offer nothing and are closed
    // rather than showing an empty field to type into.
    QVERIFY(displays(model, 0, PerfConfigEventsModel::ColumnOperation).isEmpty());
    QVERIFY(displays(model, 0, PerfConfigEventsModel::ColumnResult).isEmpty());
    QVERIFY(editable(model, 0, PerfConfigEventsModel::ColumnSubType));
    QVERIFY(!editable(model, 0, PerfConfigEventsModel::ColumnOperation));
    QVERIFY(!editable(model, 0, PerfConfigEventsModel::ColumnResult));
}

void PerfConfigEventsModelTest::testCacheEventOffersOperationAndResult()
{
    PerfSettings settings;
    settings.events.setValue({"L1-dcache-load-misses"});
    PerfConfigEventsModel model(&settings);

    QCOMPARE(model.index(0, PerfConfigEventsModel::ColumnEventType).data().toString(),
             QString("cache"));
    QCOMPARE(model.index(0, PerfConfigEventsModel::ColumnSubType).data().toString(),
             QString("L1-dcache"));
    QCOMPARE(model.index(0, PerfConfigEventsModel::ColumnOperation).data().toString(),
             QString("load"));
    QCOMPARE(model.index(0, PerfConfigEventsModel::ColumnResult).data().toString(),
             QString("misses"));

    QCOMPARE(displays(model, 0, PerfConfigEventsModel::ColumnSubType).first(),
             QString("L1-dcache"));
    QCOMPARE(displays(model, 0, PerfConfigEventsModel::ColumnOperation),
             QStringList({"load", "store", "prefetch"}));
    QCOMPARE(displays(model, 0, PerfConfigEventsModel::ColumnResult),
             QStringList({"refs", "misses"}));
    QVERIFY(editable(model, 0, PerfConfigEventsModel::ColumnOperation));
    QVERIFY(editable(model, 0, PerfConfigEventsModel::ColumnResult));
}

void PerfConfigEventsModelTest::testWrittenOutEventsGetAPatternInstead()
{
    PerfSettings settings;
    settings.events.setValue({"r0f3", "mem:0000000000401000:rw", "some-vendor-event"});
    PerfConfigEventsModel model(&settings);

    const auto validator = [&model](int row) {
        return model.index(row, PerfConfigEventsModel::ColumnSubType)
            .data(Utils::AspectTable::ValidatorRole).toString();
    };

    // A raw or breakpoint event is typed, not picked, so the cell offers no
    // choice and a pattern for what it will accept.
    QCOMPARE(model.index(0, PerfConfigEventsModel::ColumnSubType).data().toString(),
             QString("r0f3"));
    QVERIFY(displays(model, 0, PerfConfigEventsModel::ColumnSubType).isEmpty());
    QCOMPARE(validator(0), QString("r[0-9a-f]{3}"));

    QCOMPARE(model.index(1, PerfConfigEventsModel::ColumnSubType).data().toString(),
             QString("0x0000000000401000"));
    QCOMPARE(validator(1), QString("0x[0-9a-f]{16}"));
    QCOMPARE(model.index(1, PerfConfigEventsModel::ColumnOperation).data().toString(),
             QString("rw"));
    QCOMPARE(displays(model, 1, PerfConfigEventsModel::ColumnOperation),
             QStringList({"r", "rw", "rwx", "rx", "w", "wx", "x"}));

    // A custom event is free text with nothing to match.
    QCOMPARE(model.index(2, PerfConfigEventsModel::ColumnSubType).data().toString(),
             QString("some-vendor-event"));
    QVERIFY(validator(2).isEmpty());
    QVERIFY(displays(model, 2, PerfConfigEventsModel::ColumnSubType).isEmpty());
}

void PerfConfigEventsModelTest::testChoosingWritesTheEventBack()
{
    PerfSettings settings;
    settings.events.setValue({"L1-dcache-load-misses"});
    PerfConfigEventsModel model(&settings);

    const QModelIndex result = model.index(0, PerfConfigEventsModel::ColumnResult);
    QVERIFY(model.setData(result, idOf(model, 0, PerfConfigEventsModel::ColumnResult, "refs"),
                          Qt::EditRole));
    QCOMPARE(settings.events.volatileValue(), QStringList({"L1-dcache-load-refs"}));

    const QModelIndex operation = model.index(0, PerfConfigEventsModel::ColumnOperation);
    QVERIFY(model.setData(operation,
                          idOf(model, 0, PerfConfigEventsModel::ColumnOperation, "store"),
                          Qt::EditRole));
    QCOMPARE(settings.events.volatileValue(), QStringList({"L1-dcache-store-refs"}));

    // Changing the event type takes the row with it.
    const QModelIndex type = model.index(0, PerfConfigEventsModel::ColumnEventType);
    QVERIFY(model.setData(type,
                          idOf(model, 0, PerfConfigEventsModel::ColumnEventType, "software"),
                          Qt::EditRole));
    QCOMPARE(settings.events.volatileValue(), QStringList({"cpu-clock"}));
}

void PerfConfigEventsModelTest::testWrittenTextRoundTripsThroughTheModel()
{
    PerfSettings settings;
    settings.events.setValue({"r0f3", "mem:0000000000401000:rw"});
    PerfConfigEventsModel model(&settings);

    // A view hands the text over as it was typed; the model reads back the form
    // it wrote, rather than every view knowing the hex.
    QVERIFY(model.setData(model.index(0, PerfConfigEventsModel::ColumnSubType), QString("r1ab"),
                          Qt::EditRole));
    QCOMPARE(settings.events.volatileValue().at(0), QString("r1ab"));

    QVERIFY(model.setData(model.index(1, PerfConfigEventsModel::ColumnSubType),
                          QString("0x0000000000402000"), Qt::EditRole));
    QCOMPARE(settings.events.volatileValue().at(1), QString("mem:0000000000402000:rw"));

    // The numeric form the widget delegate used to hand over still works.
    QVERIFY(model.setData(model.index(0, PerfConfigEventsModel::ColumnSubType), quint64(0x2cd),
                          Qt::EditRole));
    QCOMPARE(settings.events.volatileValue().at(0), QString("r2cd"));
}

QObject *createPerfConfigEventsModelTest()
{
    return new PerfConfigEventsModelTest;
}

} // namespace Profiler::Internal

#include "perfconfigeventsmodel_test.moc"

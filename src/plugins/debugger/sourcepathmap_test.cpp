// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifdef WITH_TESTS

#include "sourcepathmap_test.h"

#include "commonoptionspage.h"

#include <utils/aspectpresentation.h>

#include <QAbstractItemModel>
#include <QTest>

using namespace Utils;

namespace Debugger::Internal {

// The aspect's value is a map keyed by source path; what it shows is rows. The
// rows are what can be half typed, so they are the aspect's own state and the
// map is derived from them.
class SourcePathMapTest : public QObject
{
    Q_OBJECT

private slots:
    void testRowsAndValueRoundTrip();
    void testHalfFilledRowStaysARowButNotAMapping();
    void testAddedRowsAreEditableAndEmpty();
    void testQtSourcesAddOneRowPerBuildPath();
};

enum { SourceColumn, TargetColumn };

static QStringList rowsOf(QAbstractItemModel *model)
{
    QStringList rows;
    for (int row = 0; row < model->rowCount({}); ++row) {
        rows << model->index(row, SourceColumn).data().toString() + " -> "
                    + model->index(row, TargetColumn).data().toString();
    }
    return rows;
}

void SourcePathMapTest::testRowsAndValueRoundTrip()
{
    AspectContainer page;
    SourcePathMapAspect aspect(&page);
    aspect.setValue({{"/build/one", "/src/one"}, {"/build/two", "/src/two"}});

    QAbstractItemModel *model = aspect.tableModel();
    QVERIFY(model);
    QCOMPARE(model->columnCount({}), 2);
    QCOMPARE(rowsOf(model), QStringList({"/build/one -> /src/one", "/build/two -> /src/two"}));
    QCOMPARE(model->headerData(SourceColumn, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Source path"));

    // Editing a cell is what makes the aspect dirty, and applying moves the
    // rows into the value.
    QVERIFY(!aspect.isDirty());
    QVERIFY(model->setData(model->index(0, TargetColumn), QString("/elsewhere"), Qt::EditRole));
    QVERIFY(aspect.isDirty());
    aspect.apply();
    QCOMPARE(aspect.value(), SourcePathMap({{"/build/one", "/elsewhere"},
                                            {"/build/two", "/src/two"}}));
    QVERIFY(!aspect.isDirty());
}

void SourcePathMapTest::testHalfFilledRowStaysARowButNotAMapping()
{
    AspectContainer page;
    SourcePathMapAspect aspect(&page);
    aspect.setValue({{"/build/one", "/src/one"}});

    QAbstractItemModel *model = aspect.tableModel();
    QVERIFY(model->insertRows(model->rowCount({}), 1, {}));
    QVERIFY(model->setData(model->index(1, SourceColumn), QString("/build/two"), Qt::EditRole));

    // A row with no target yet is a row the user is still typing into, so it
    // stays put - but it is not a mapping, and it may not push the finished one
    // out of a map that is keyed by source path.
    QCOMPARE(model->rowCount({}), 2);
    aspect.apply();
    QCOMPARE(aspect.value(), SourcePathMap({{"/build/one", "/src/one"}}));

    // Finishing it makes it one.
    QVERIFY(model->setData(model->index(1, TargetColumn), QString("/src/two"), Qt::EditRole));
    aspect.apply();
    QCOMPARE(aspect.value(), SourcePathMap({{"/build/one", "/src/one"},
                                            {"/build/two", "/src/two"}}));

    // And removing it takes it away again.
    QVERIFY(model->removeRows(1, 1, {}));
    aspect.apply();
    QCOMPARE(aspect.value(), SourcePathMap({{"/build/one", "/src/one"}}));
}

void SourcePathMapTest::testAddedRowsAreEditableAndEmpty()
{
    AspectContainer page;
    SourcePathMapAspect aspect(&page);
    aspect.setValue({});

    QAbstractItemModel *model = aspect.tableModel();
    QCOMPARE(model->rowCount({}), 0);
    QVERIFY(model->insertRows(0, 1, {}));

    // Empty, rather than the "<new source>" text the widget used to write in
    // and then recognize by its angle brackets - which made any path starting
    // with one look unfinished.
    QCOMPARE(rowsOf(model), QStringList({" -> "}));
    for (int column : {SourceColumn, TargetColumn}) {
        const QModelIndex index = model->index(0, column);
        QVERIFY(index.flags().testFlag(Qt::ItemIsEditable));
        QVERIFY(index.data(AspectTable::EditableRole).toBool());
    }

    // Two unfinished rows are two rows, though they would be one map entry.
    QVERIFY(model->insertRows(1, 1, {}));
    QCOMPARE(model->rowCount({}), 2);
    aspect.apply();
    QCOMPARE(aspect.value(), SourcePathMap());
}

void SourcePathMapTest::testQtSourcesAddOneRowPerBuildPath()
{
    AspectContainer page;
    SourcePathMapAspect aspect(&page);
    aspect.setValue({});

    // Every host has some; the action that calls this is hidden if not.
    QVERIFY(SourcePathMapAspect::hasQtBuildPaths());

    QAbstractItemModel *model = aspect.tableModel();
    aspect.addQtSources(FilePath::fromUserInput("/home/me/qt"));
    QVERIFY(model->rowCount({}) > 0);

    aspect.apply();
    const SourcePathMap map = aspect.value();
    QCOMPARE(map.size(), model->rowCount({}));
    for (const QString &target : map.values())
        QCOMPARE(target, QString("/home/me/qt"));
}

QObject *createSourcePathMapTest()
{
    return new SourcePathMapTest;
}

} // namespace Debugger::Internal

#include "sourcepathmap_test.moc"

#endif // WITH_TESTS

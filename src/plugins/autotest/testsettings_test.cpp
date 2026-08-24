// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "testsettings_test.h"

#include "testframeworkmanager.h"
#include "testsettings.h"

#include <utils/aspectpresentation.h>

#include <QAbstractItemModel>
#include <QTest>

using namespace Utils;

namespace Autotest::Internal {

// FrameworksAspect used to keep its value in the tree widget it built, so
// isDirty() and cancel() only worked while its page was open. It owns the check
// states now; these are the parts that made it renderer-independent.
class TestSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testRowPerFrameworkAndTool();
    void testTickingIsVolatileUntilCancelled();
    void testWarningFollowsWhatIsTicked();
};

enum { ColumnActive, ColumnGrouping };

// cancel() and isDirty() are the framework's entry points, and the aspect keeps
// its overrides private the way it always did.
static BaseAspect &framework(FrameworksAspect &aspect)
{
    return aspect;
}

static int frameworkCount()
{
    return TestFrameworkManager::registeredFrameworks().size();
}

static int rowCount()
{
    return frameworkCount() + TestFrameworkManager::registeredTestTools().size();
}

// Sets every framework and tool row's active cell, so that a test can reach a
// known state without knowing which frameworks the build registered.
static void setAllActive(QAbstractItemModel *model, bool frameworks, bool tools)
{
    for (int row = 0; row < model->rowCount({}); ++row) {
        const bool on = row < frameworkCount() ? frameworks : tools;
        model->setData(model->index(row, ColumnActive),
                       on ? Qt::Checked : Qt::Unchecked,
                       Qt::CheckStateRole);
    }
}

void TestSettingsTest::testRowPerFrameworkAndTool()
{
    QAbstractItemModel *model = testSettings().frameworks.tableModel();
    QVERIFY(model);
    QVERIFY(frameworkCount() > 0);
    QCOMPARE(model->rowCount({}), rowCount());
    QCOMPARE(model->columnCount({}), 2);

    // The first column names the row and turns it on.
    const QModelIndex active = model->index(0, ColumnActive);
    QVERIFY(!active.data(Qt::DisplayRole).toString().isEmpty());
    QVERIFY(active.data(AspectTable::CheckableRole).toBool());
    QVERIFY(active.data(AspectTable::EditableRole).toBool());

    // A framework's tests can be grouped; a test tool has no tests of its own,
    // so its second cell is empty rather than an unusable check box.
    const QModelIndex grouping = model->index(0, ColumnGrouping);
    QVERIFY(grouping.data(AspectTable::CheckableRole).toBool());
    QVERIFY(grouping.data(Qt::CheckStateRole).isValid());

    if (rowCount() > frameworkCount()) {
        const QModelIndex toolGrouping = model->index(frameworkCount(), ColumnGrouping);
        QVERIFY(!toolGrouping.data(AspectTable::CheckableRole).toBool());
        QVERIFY(!toolGrouping.data(Qt::CheckStateRole).isValid());
        QVERIFY(!toolGrouping.flags().testFlag(Qt::ItemIsUserCheckable));
    }
}

void TestSettingsTest::testTickingIsVolatileUntilCancelled()
{
    FrameworksAspect &aspect = testSettings().frameworks;
    QAbstractItemModel *model = aspect.tableModel();

    // Nothing has been touched, so nothing is dirty - and that is now an answer
    // the aspect can give whether or not anything is drawing it.
    framework(aspect).cancel();
    QVERIFY(!framework(aspect).isDirty());

    const QModelIndex active = model->index(0, ColumnActive);
    const bool was = active.data(Qt::CheckStateRole) == Qt::Checked;
    QVERIFY(model->setData(active, was ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(active.data(Qt::CheckStateRole) == Qt::Checked, !was);
    QVERIFY(framework(aspect).isDirty());

    // The applied value has not moved: framework() still answers what was
    // applied, not what is ticked.
    QCOMPARE(aspect.framework(TestFrameworkManager::registeredFrameworks().first()->id()), was);

    // Cancelling puts the check box back without a page having to repopulate it.
    framework(aspect).cancel();
    QVERIFY(!framework(aspect).isDirty());
    QCOMPARE(active.data(Qt::CheckStateRole) == Qt::Checked, was);
}

void TestSettingsTest::testWarningFollowsWhatIsTicked()
{
    FrameworksAspect &aspect = testSettings().frameworks;
    QAbstractItemModel *model = aspect.tableModel();

    setAllActive(model, false, false);
    QCOMPARE(aspect.warning(), QString("No active test frameworks or tools."));
    QVERIFY(!aspect.warningToolTip().isEmpty());

    setAllActive(model, true, false);
    QCOMPARE(aspect.warning(), QString());
    QVERIFY(aspect.warningToolTip().isEmpty());

    if (rowCount() > frameworkCount()) {
        setAllActive(model, false, true);
        QCOMPARE(aspect.warning(), QString());

        // Both kinds at once duplicates run information, which is worth saying.
        setAllActive(model, true, true);
        QCOMPARE(aspect.warning(), QString("Mixing test frameworks and test tools."));
    }

    framework(aspect).cancel();
}

QObject *createTestSettingsTest()
{
    return new TestSettingsTest;
}

} // namespace Autotest::Internal

#include "testsettings_test.moc"

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmljseditorsettings_test.h"

#include "qmljseditorsettings.h"

#include <utils/aspectpresentation.h>

#include <QAbstractItemModel>
#include <QTest>

using namespace Utils;

namespace QmlJSEditor::Internal {

// AnalyzerMessagesAspect used to keep its value in the tree it built, so
// apply(), cancel() and isDirty() only worked while its page was open - and
// apply() silently did nothing when it was not. It owns the check states now.
class QmlJsEditingSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testRowPerMessage();
    void testEnabledColumnStoresTheOpposite();
    void testTickingIsVolatileUntilApplied();
    void testResetToDefault();
};

enum { ColumnEnabled, ColumnNonQuickUi, ColumnMessage };

static bool checked(QAbstractItemModel *model, int row, int column)
{
    return model->index(row, column).data(Qt::CheckStateRole).toInt() == Qt::Checked;
}

static void setChecked(QAbstractItemModel *model, int row, int column, bool on)
{
    QVERIFY(model->setData(model->index(row, column),
                           on ? Qt::Checked : Qt::Unchecked,
                           Qt::CheckStateRole));
}

void QmlJsEditingSettingsTest::testRowPerMessage()
{
    AspectContainer page;
    AnalyzerMessagesAspect aspect(&page);
    aspect.readSettings();

    QAbstractItemModel *model = aspect.tableModel();
    QVERIFY(model);
    QVERIFY(model->rowCount({}) > 0);
    QCOMPARE(model->columnCount({}), 3);

    // The first column names the message and turns the check on, the second
    // turns it off outside a Qt Quick UI, and the third is the message to read.
    QVERIFY(model->index(0, ColumnEnabled).data().toString().startsWith("M"));
    QVERIFY(!model->index(0, ColumnMessage).data().toString().isEmpty());
    QVERIFY(model->index(0, ColumnEnabled).data(AspectTable::CheckableRole).toBool());
    QVERIFY(model->index(0, ColumnNonQuickUi).data(AspectTable::CheckableRole).toBool());

    // Nothing to check on the message itself.
    QVERIFY(!model->index(0, ColumnMessage).data(AspectTable::CheckableRole).toBool());
    QVERIFY(!model->index(0, ColumnMessage).data(AspectTable::EditableRole).toBool());
    QCOMPARE(model->headerData(ColumnEnabled, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Enabled"));
    QCOMPARE(model->headerData(ColumnMessage, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Message"));
}

void QmlJsEditingSettingsTest::testEnabledColumnStoresTheOpposite()
{
    AspectContainer page;
    AnalyzerMessagesAspect aspect(&page);
    aspect.readSettings();

    QAbstractItemModel *model = aspect.tableModel();

    // What is stored is the list of *disabled* messages, so the Enabled column
    // shows the opposite of what it writes. Getting that backwards would turn
    // every check off the first time anything was touched.
    setChecked(model, 0, ColumnEnabled, true);
    QVERIFY(checked(model, 0, ColumnEnabled));
    setChecked(model, 0, ColumnEnabled, false);
    QVERIFY(!checked(model, 0, ColumnEnabled));
    setChecked(model, 0, ColumnEnabled, true);
    QVERIFY(checked(model, 0, ColumnEnabled));

    // The second column stores what it shows, and the two are independent.
    setChecked(model, 0, ColumnNonQuickUi, true);
    QVERIFY(checked(model, 0, ColumnNonQuickUi));
    QVERIFY(checked(model, 0, ColumnEnabled));
    setChecked(model, 0, ColumnNonQuickUi, false);
    QVERIFY(!checked(model, 0, ColumnNonQuickUi));
    QVERIFY(checked(model, 0, ColumnEnabled));

    // Ticking one row leaves its neighbour alone.
    QVERIFY(model->rowCount({}) > 1);
    const bool second = checked(model, 1, ColumnEnabled);
    setChecked(model, 0, ColumnEnabled, false);
    QCOMPARE(checked(model, 1, ColumnEnabled), second);
}

void QmlJsEditingSettingsTest::testTickingIsVolatileUntilApplied()
{
    AspectContainer page;
    AnalyzerMessagesAspect aspect(&page);
    aspect.readSettings();

    BaseAspect &framework = aspect;
    QAbstractItemModel *model = aspect.tableModel();
    QVERIFY(!framework.isDirty());

    const bool was = checked(model, 0, ColumnEnabled);
    setChecked(model, 0, ColumnEnabled, !was);
    QVERIFY(framework.isDirty());

    // Cancelling puts the check box back, which used to need the tree that held
    // the value to still exist.
    framework.cancel();
    QVERIFY(!framework.isDirty());
    QCOMPARE(checked(model, 0, ColumnEnabled), was);

    // Applying keeps it, and there is nothing left to apply afterwards.
    setChecked(model, 0, ColumnEnabled, !was);
    framework.apply();
    QVERIFY(!framework.isDirty());
    QCOMPARE(checked(model, 0, ColumnEnabled), !was);
    framework.cancel();
    QCOMPARE(checked(model, 0, ColumnEnabled), !was);
}

void QmlJsEditingSettingsTest::testResetToDefault()
{
    AspectContainer page;
    AnalyzerMessagesAspect aspect(&page);
    aspect.readSettings();

    BaseAspect &framework = aspect;
    QAbstractItemModel *model = aspect.tableModel();
    framework.apply();

    // Turn everything on, which is not the default: some checks are off out of
    // the box.
    for (int row = 0; row < model->rowCount({}); ++row)
        setChecked(model, row, ColumnEnabled, true);
    framework.apply();

    aspect.resetToDefault();
    QVERIFY(framework.isDirty());
    const bool anyOff = [&] {
        for (int row = 0; row < model->rowCount({}); ++row) {
            if (!checked(model, row, ColumnEnabled))
                return true;
        }
        return false;
    }();
    QVERIFY2(anyOff, "resetting turned nothing off, so it did not restore the defaults");
}

QObject *createQmlJsEditingSettingsTest()
{
    return new QmlJsEditingSettingsTest;
}

} // namespace QmlJSEditor::Internal

#include "qmljseditorsettings_test.moc"

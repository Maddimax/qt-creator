// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "macrooptions_test.h"

#include "macrooptionspage.h"

#include <utils/aspectpresentation.h>
#include <utils/aspects.h>

#include <QAbstractItemModel>
#include <QTest>

using namespace Utils;

namespace Macros::Internal {

class MacroOptionsTest final : public QObject
{
    Q_OBJECT

private slots:
    // What the page shows of a macro: its name, what it does, and how it is
    // invoked. The name and the shortcut are not the page's to change.
    void testTheTableShowsWhatAMacroIs()
    {
        AspectContainer page;
        MacrosAspect macros(&page);
        macros.setValue({{"first", "does a thing"}, {"second", "does another"}});

        QAbstractItemModel *model = macros.tableModel();
        QVERIFY(model);
        QCOMPARE(model->columnCount(), 3);
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->headerData(0, Qt::Horizontal).toString(), QString("Name"));
        QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QString("Description"));
        QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), QString("Shortcut"));

        QCOMPARE(model->index(0, 0).data().toString(), QString("first"));
        QCOMPARE(model->index(0, 1).data().toString(), QString("does a thing"));

        // A macro that is not on disk is not one the page may write to, so its
        // description is not offered for editing either.
        QVERIFY(!model->index(0, 0).data(AspectTable::EditableRole).toBool());
        QVERIFY(!model->index(0, 1).data(AspectTable::EditableRole).toBool());
    }

    // Removing is deferred: it takes the macro out of what the page holds, and
    // apply() works out from that which ones to delete.
    void testRemovingARowTakesItOutOfTheValue()
    {
        AspectContainer page;
        page.setAutoApply(false);
        MacrosAspect macros(&page);
        macros.setValue({{"first", "does a thing"}, {"second", "does another"}});
        QVERIFY(!macros.isDirty());

        QAbstractItemModel *model = macros.tableModel();
        QVERIFY(model->removeRows(0, 1));

        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data().toString(), QString("second"));

        // The value the page would commit no longer has it, and says so.
        QVERIFY(macros.isDirty());
        QCOMPARE(macros.volatileValue().keys(), QStringList({"second"}));
        // And the macro itself is untouched until apply().
        QCOMPARE(macros.value().keys(), QStringList({"first", "second"}));
    }

    // Cancel puts back what the macros hold, not what was left in the table.
    void testCancelPutsTheRowsBack()
    {
        AspectContainer page;
        page.setAutoApply(false);
        MacrosAspect macros(&page);
        macros.setValue({{"first", "does a thing"}, {"second", "does another"}});

        QVERIFY(macros.tableModel()->removeRows(0, 1));
        QCOMPARE(macros.tableModel()->rowCount(), 1);

        macros.cancel();
        QCOMPARE(macros.tableModel()->rowCount(), 2);
        QVERIFY(!macros.isDirty());
    }
};

QObject *createMacroOptionsTest()
{
    return new MacroOptionsTest;
}

} // namespace Macros::Internal

#include "macrooptions_test.moc"

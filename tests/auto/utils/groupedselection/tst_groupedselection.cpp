// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/groupedmodel.h>
#include <utils/groupedselection.h>

#include <QSignalSpy>
#include <QTest>

using namespace Utils;

namespace {

class Tool
{
public:
    friend bool operator==(const Tool &, const Tool &) = default;

    QString name;
    bool autoDetected = false;
};

} // namespace

Q_DECLARE_METATYPE(Tool)

namespace {

// The shape every page using GroupedModel has: what was found, and what the
// user added.
class ToolsModel : public TypedGroupedModel<Tool>
{
public:
    ToolsModel()
    {
        setShowDefault(true);
        setHeader({"Name"});
        setFilters("Auto-detected", {{"Manual", [this](int row) {
                                          return !item(row).autoDetected;
                                      }}});
    }

    int cloneRow(int row) override
    {
        return appendVolatileItem({item(row).name + " (copy)", false});
    }

    using TypedGroupedModel<Tool>::setDefaultRow;

private:
    QVariant variantData(int row, int column, int role) const override
    {
        if (role == Qt::DisplayRole && column == 0)
            return item(row).name;
        return {};
    }
};

} // namespace

// GroupedSelection is what a view of a GroupedModel does when it is not
// drawing: which item is current, what may be done to it, and where to go once
// it has been removed. It used to be inside GroupedView, so none of it could
// be checked without a QTreeView on screen.
class tst_GroupedSelection : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void testNothingIsCurrentToBeginWith();
    void testTheActionsFollowWhatIsCurrent();
    void testAPageCanRefuseToRemoveOrCloneARow();
    void testRemovingMovesOnWithinTheGroup();
    void testRemovingTheLastOfAGroupMovesToAnother();
    void testRemovalIsUndoneRatherThanDoneTwice();
    void testRemovingTheDefaultAndRestoringItGivesItBack();
    void testCloningSelectsTheCopy();
    void testCurrentSurvivesAResetByValue();

private:
    std::unique_ptr<ToolsModel> m_model;
    std::unique_ptr<GroupedSelection> m_selection;

    int rowOf(const QString &name) const
    {
        for (int row = 0; row < m_model->itemCount(); ++row) {
            if (m_model->item(row).name == name)
                return row;
        }
        return -1;
    }
};

void tst_GroupedSelection::init()
{
    m_model = std::make_unique<ToolsModel>();
    m_model->appendItem({"found-a", true});
    m_model->appendItem({"found-b", true});
    m_model->appendItem({"mine-a", false});
    m_model->appendItem({"mine-b", false});
    m_model->setDefaultRow(rowOf("found-a"));
    m_selection = std::make_unique<GroupedSelection>(*m_model);
}

void tst_GroupedSelection::testNothingIsCurrentToBeginWith()
{
    QCOMPARE(m_selection->currentRow(), -1);
    QVERIFY(!m_selection->canRemoveCurrent());
    QVERIFY(!m_selection->canCloneCurrent());
    QVERIFY(!m_selection->canMakeCurrentDefault());
    QVERIFY(!m_selection->currentIsRemoved());
}

void tst_GroupedSelection::testTheActionsFollowWhatIsCurrent()
{
    QSignalSpy actions(m_selection.get(), &GroupedSelection::actionsChanged);
    QSignalSpy current(m_selection.get(), &GroupedSelection::currentRowChanged);

    m_selection->setCurrentRow(rowOf("mine-a"));
    QCOMPARE(current.size(), 1);
    QCOMPARE(current.first().at(0).toInt(), -1);
    QCOMPARE(current.first().at(1).toInt(), rowOf("mine-a"));
    QVERIFY(!actions.isEmpty());
    QVERIFY(m_selection->canRemoveCurrent());
    QVERIFY(m_selection->canCloneCurrent());
    // Not the default one, so it can be made it.
    QVERIFY(m_selection->canMakeCurrentDefault());

    // And the one that already is cannot.
    m_selection->setCurrentRow(rowOf("found-a"));
    QVERIFY(!m_selection->canMakeCurrentDefault());

    // Setting the same row again says nothing.
    const int before = current.size();
    m_selection->setCurrentRow(rowOf("found-a"));
    QCOMPARE(current.size(), before);
}

void tst_GroupedSelection::testAPageCanRefuseToRemoveOrCloneARow()
{
    m_selection->setCanRemoveRow([this](int row) { return !m_model->item(row).autoDetected; });
    m_selection->setCanCloneRow([this](int row) { return !m_model->item(row).autoDetected; });

    m_selection->setCurrentRow(rowOf("found-a"));
    QVERIFY(!m_selection->canRemoveCurrent());
    QVERIFY(!m_selection->canCloneCurrent());

    m_selection->setCurrentRow(rowOf("mine-a"));
    QVERIFY(m_selection->canRemoveCurrent());
    QVERIFY(m_selection->canCloneCurrent());
}

void tst_GroupedSelection::testRemovingMovesOnWithinTheGroup()
{
    // Removal keeps the item in the list, struck through, so staying put would
    // make removing several in a row do nothing after the first.
    m_selection->setCurrentRow(rowOf("mine-a"));
    m_selection->removeCurrent();

    QCOMPARE(m_selection->currentRow(), rowOf("mine-b"));
    QVERIFY(m_model->isRemoved(rowOf("mine-a")));
    QVERIFY(!m_selection->currentIsRemoved());
}

void tst_GroupedSelection::testRemovingTheLastOfAGroupMovesToAnother()
{
    m_selection->setCurrentRow(rowOf("mine-a"));
    m_selection->removeCurrent();
    m_selection->setCurrentRow(rowOf("mine-b"));
    m_selection->removeCurrent();

    // Nothing is left in "Manual", so it moves to the other group rather than
    // to a row that is on its way out.
    const int row = m_selection->currentRow();
    QVERIFY(row >= 0);
    QVERIFY(m_model->item(row).autoDetected);
    QVERIFY(!m_model->isRemoved(row));
}

void tst_GroupedSelection::testRemovalIsUndoneRatherThanDoneTwice()
{
    const int row = rowOf("mine-a");
    m_selection->setCurrentRow(row);
    m_selection->removeCurrent();

    m_selection->setCurrentRow(row);
    QVERIFY(m_selection->currentIsRemoved());
    // A row on its way out can always come back, whatever the page says about
    // removing it.
    m_selection->setCanRemoveRow([](int) { return false; });
    QVERIFY(m_selection->canRemoveCurrent());
    // And nothing else may be done to it while it is going.
    QVERIFY(!m_selection->canCloneCurrent());
    QVERIFY(!m_selection->canMakeCurrentDefault());

    m_selection->removeCurrent();
    QVERIFY(!m_model->isRemoved(row));
    // Restoring stays on the row that came back.
    QCOMPARE(m_selection->currentRow(), row);
}

void tst_GroupedSelection::testRemovingTheDefaultAndRestoringItGivesItBack()
{
    const int row = rowOf("found-a");
    QVERIFY(m_model->isDefault(row));

    m_selection->setCurrentRow(row);
    m_selection->removeCurrent();
    QVERIFY(!m_model->isDefault(row));

    m_selection->setCurrentRow(row);
    m_selection->removeCurrent();
    QVERIFY(m_model->isDefault(row));
}

void tst_GroupedSelection::testCloningSelectsTheCopy()
{
    QSignalSpy cloned(m_selection.get(), &GroupedSelection::currentCloned);
    m_selection->setCurrentRow(rowOf("mine-a"));
    m_selection->cloneCurrent();

    QCOMPARE(cloned.size(), 1);
    QCOMPARE(m_selection->currentRow(), rowOf("mine-a (copy)"));
    QVERIFY(m_selection->currentRow() >= 0);
}

void tst_GroupedSelection::testCurrentSurvivesAResetByValue()
{
    m_selection->setCurrentRow(rowOf("mine-b"));
    const Tool wanted = m_model->item(m_selection->currentRow());
    QSignalSpy reset(m_model->groupedDisplayModel(), &QAbstractItemModel::modelReset);

    // Replacing the items rebuilds the tree, so the row that was current is
    // gone - and it is not even the same row number any more. What was in it
    // is still there, which is what the selection goes by.
    QList<Tool> reordered = m_model->items();
    std::reverse(reordered.begin(), reordered.end());
    m_model->setItems(reordered);

    QCOMPARE(reset.size(), 1);
    QVERIFY(m_selection->currentRow() >= 0);
    QCOMPARE(m_model->item(m_selection->currentRow()), wanted);
    // Really a different row, or this could not tell a working restore from
    // one that simply left the number alone.
    QCOMPARE(m_selection->currentRow(), rowOf("mine-b"));
    QVERIFY(rowOf("mine-b") != 3);
}

QTEST_GUILESS_MAIN(tst_GroupedSelection)

#include "tst_groupedselection.moc"

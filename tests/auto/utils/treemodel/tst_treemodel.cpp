// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/treemodel.h>

#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStringListModel>
#include <QTest>

#include <type_traits>

//TESTED_COMPONENT=src/libs/utils/treemodel

using namespace Utils;

class tst_TreeModel : public QObject
{
    Q_OBJECT

private slots:
    void testTypes();
    void testIteration();
    void testMixed();
    void testRemoveRows();
    void testRoleNames();
    void testNamedRoles();
};

static int countLevelItems(TreeItem *base, int level)
{
    int n = 0;
    int bl = base->level();
    base->forAllChildren([level, bl, &n](TreeItem *item) {
        if (item->level() == bl + level)
            ++n;
    });
    return n;
}

static TreeItem *createItem(const QString &name)
{
    return new StaticTreeItem(name);
}

void tst_TreeModel::testIteration()
{
    TreeModel<> m;
    TreeItem *r = m.rootItem();
    TreeItem *group0 = createItem("group0");
    TreeItem *group1 = createItem("group1");
    TreeItem *item10 = createItem("item10");
    TreeItem *item11 = createItem("item11");
    TreeItem *item12 = createItem("item12");
    group1->appendChild(item10);
    group1->appendChild(item11);
    TreeItem *group2 = createItem("group2");
    TreeItem *item20 = createItem("item20");
    TreeItem *item21 = createItem("item21");
    TreeItem *item22 = createItem("item22");
    r->appendChild(group0);
    r->appendChild(group1);
    r->appendChild(group2);
    group1->appendChild(item12);
    group2->appendChild(item20);
    group2->appendChild(item21);
    group2->appendChild(item22);

    QCOMPARE(r->childCount(), 3);
    QCOMPARE(countLevelItems(r, 1), 3);
    QCOMPARE(countLevelItems(r, 2), 6);
    QCOMPARE(countLevelItems(r, 3), 0);
    QCOMPARE(countLevelItems(group0, 1), 0);
    QCOMPARE(countLevelItems(group1, 1), 3);
    QCOMPARE(countLevelItems(group1, 2), 0);
    QCOMPARE(countLevelItems(group2, 1), 3);
    QCOMPARE(countLevelItems(group2, 2), 0);
}

struct ItemA : public TreeItem {};
struct ItemB : public TreeItem {};

void tst_TreeModel::testMixed()
{
    TreeModel<TreeItem, ItemA, ItemB> m;
    TreeItem *r = m.rootItem();
    TreeItem *ra;
    r->appendChild(new ItemA);
    r->appendChild(ra = new ItemA);
    ra->appendChild(new ItemB);
    ra->appendChild(new ItemB);

    int n = 0;
    m.forItemsAtLevel<1>([&n](ItemA *) { ++n; });
    QCOMPARE(n, 2);

    n = 0;
    m.forItemsAtLevel<2>([&n](ItemB *) { ++n; });
    QCOMPARE(n, 2);
}

void tst_TreeModel::testRemoveRows()
{
    TreeModel<> m;
    TreeItem *r = m.rootItem();
    for (int i = 0; i < 5; ++i)                     // item0 .. item4
        r->appendChild(createItem(QString("item%1").arg(i)));
    QCOMPARE(m.rowCount(), 5);

    // Direct removal of a middle range.
    QVERIFY(m.removeRows(1, 2));                     // drops item1, item2
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(m.index(0, 0).data().toString(), QString("item0"));
    QCOMPARE(m.index(1, 0).data().toString(), QString("item3"));
    QCOMPARE(m.index(2, 0).data().toString(), QString("item4"));

    // Out-of-range and empty requests are rejected without changing anything.
    QVERIFY(!m.removeRows(2, 5));
    QVERIFY(!m.removeRows(-1, 1));
    QVERIFY(!m.removeRows(0, 0));
    QCOMPARE(m.rowCount(), 3);

    // Removal through a QSortFilterProxyModel must reach the source model.
    // The Valgrind suppression dialog relies on exactly this path
    // (QTCREATORBUG-18041).
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&m);
    QCOMPARE(proxy.rowCount(), 3);
    QVERIFY(proxy.removeRow(0));                     // drops item0 via the proxy
    QCOMPARE(proxy.rowCount(), 2);
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.index(0, 0).data().toString(), QString("item3"));
}

// Resolves a role by name, the way QQmlDelegateModel binds delegate
// properties.
static int roleByName(const QAbstractItemModel &model, const QByteArray &name)
{
    const QHash<int, QByteArray> names = model.roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        if (it.value() == name)
            return it.key();
    }
    return -1;
}

void tst_TreeModel::testRoleNames()
{
    // An unconfigured model keeps the stock QAbstractItemModel role names.
    TreeModel<> unconfigured;
    QCOMPARE(unconfigured.roleNames(), QStringListModel().roleNames());

    TreeModel<> m;
    const int kindRole = m.addColumnRole("kind", 1);
    const int detailRole = m.addItemRole("detail");
    QVERIFY(kindRole != -1);
    QVERIFY(detailRole != -1);
    QVERIFY(kindRole != detailRole);

    const QHash<int, QByteArray> names = m.roleNames();
    QCOMPARE(names.value(kindRole), QByteArray("kind"));
    QCOMPARE(names.value(detailRole), QByteArray("detail"));

    // The defaults survive configuration.
    QCOMPARE(names.value(Qt::DisplayRole), QByteArray("display"));
    QCOMPARE(names.value(Qt::DecorationRole), QByteArray("decoration"));
    QCOMPARE(names.value(Qt::ToolTipRole), QByteArray("toolTip"));

    // Duplicate names are rejected.
    QTest::ignoreMessage(QtDebugMsg, QRegularExpression("SOFT ASSERT.*"));
    QCOMPARE(m.addItemRole("kind"), -1);
    QCOMPARE(m.roleNames().size(), names.size());
}

class NamedDataItem : public TreeItem
{
public:
    QVariant data(int column, int role) const override
    {
        if (role == Qt::DisplayRole)
            return column == 1 ? m_kind : QString("col%1").arg(column);
        return {};
    }

    bool setData(int column, const QVariant &data, int role) override
    {
        if (column == 1 && role == Qt::DisplayRole) {
            m_kind = data.toString();
            return true;
        }
        return false;
    }

    QVariant namedData(const QByteArray &roleName) const override
    {
        if (roleName == "detail")
            return QString("detail-value");
        return {};
    }

private:
    QString m_kind = "col1";
};

void tst_TreeModel::testNamedRoles()
{
    TreeModel<> m;
    m.setHeader({"name", "kind"});
    const int kindRole = m.addColumnRole("kind", 1);
    const int detailRole = m.addItemRole("detail");
    m.rootItem()->appendChild(new NamedDataItem);

    QCOMPARE(roleByName(m, "kind"), kindRole);
    QCOMPARE(roleByName(m, "detail"), detailRole);

    // QML queries column 0; a column role reroutes to the mapped column
    // and returns exactly what column-oriented data() returns there.
    const QModelIndex idx = m.index(0, 0);
    QCOMPARE(m.data(idx, kindRole), m.data(m.index(0, 1), Qt::DisplayRole));
    QCOMPARE(m.data(idx, kindRole).toString(), QString("col1"));

    // An item role bypasses the column axis entirely.
    QCOMPARE(m.data(idx, detailRole).toString(), QString("detail-value"));

    // Unregistered roles still reach TreeItem::data() unchanged.
    QCOMPARE(m.data(idx, Qt::DisplayRole).toString(), QString("col0"));
    QVERIFY(!m.data(idx, Qt::ToolTipRole).isValid());

    // Writing through a column role reaches TreeItem::setData() of the
    // mapped column.
    QVERIFY(m.setData(idx, QString("edited"), kindRole));
    QCOMPARE(m.data(idx, kindRole).toString(), QString("edited"));
    QCOMPARE(m.data(m.index(0, 1), Qt::DisplayRole).toString(), QString("edited"));
}

void tst_TreeModel::testTypes()
{
    struct A {};
    struct B {};
    struct C {};

    static_assert(std::is_same<Internal::SelectType<0, A>::Type, A>::value, "");
    static_assert(std::is_same<Internal::SelectType<0>::Type, TreeItem>::value, "");
    static_assert(std::is_same<Internal::SelectType<1>::Type, TreeItem>::value, "");
    static_assert(std::is_same<Internal::SelectType<0, A, B, C>::Type, A>::value, "");
    static_assert(std::is_same<Internal::SelectType<1, A, B, C>::Type, B>::value, "");
    static_assert(std::is_same<Internal::SelectType<2, A, B, C>::Type, C>::value, "");
    static_assert(std::is_same<Internal::SelectType<3, A, B, C>::Type, TreeItem>::value, "");
}

QTEST_GUILESS_MAIN(tst_TreeModel)

#include "tst_treemodel.moc"

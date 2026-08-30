// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "consoleitemmodel.h"

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QFontMetrics>
#include <QFont>

namespace Debugger::Internal {

ConsoleItemModel::ConsoleItemModel(QObject *parent) :
    Utils::TreeModel<>(new ConsoleItem, parent)
{
    clear();
}

QHash<int, QByteArray> ConsoleItemModel::roleNames() const
{
    QHash<int, QByteArray> names = TreeModel::roleNames();
    names.insert(ConsoleItem::TypeRole, "itemType");
    names.insert(ConsoleItem::FileRole, "file");
    names.insert(ConsoleItem::FileNameRole, "fileName");
    names.insert(ConsoleItem::LineRole, "line");
    names.insert(ConsoleItem::ExpressionRole, "expression");
    names.insert(ConsoleItem::TextColorRole, "textColor");
    return names;
}

void ConsoleItemModel::clear()
{
    Utils::TreeModel<>::clear();
    appendItem(new ConsoleItem(ConsoleItem::InputType));
    emit selectEditableRow(index(0, 0, QModelIndex()), QItemSelectionModel::ClearAndSelect);
}

void ConsoleItemModel::setCanFetchMore(bool canFetchMore)
{
    m_canFetchMore = canFetchMore;
}

bool ConsoleItemModel::canFetchMore(const QModelIndex &parent) const
{
    return m_canFetchMore && TreeModel::canFetchMore(parent);
}

void ConsoleItemModel::appendItem(ConsoleItem *item, int position)
{
    if (position < 0)
        position = rootItem()->childCount() - 1; // append before editable row

    if (position < 0)
        position = 0;

    rootItem()->insertChild(position, item);
}

void ConsoleItemModel::shiftEditableRow()
{
    int position = rootItem()->childCount();
    Q_ASSERT(position > 0);

    appendItem(new ConsoleItem(ConsoleItem::InputType), position);
    emit selectEditableRow(index(position, 0, QModelIndex()), QItemSelectionModel::ClearAndSelect);
}

int ConsoleItemModel::sizeOfFile(const QFont &font)
{
    int lastReadOnlyRow = rootItem()->childCount();
    lastReadOnlyRow -= 2; // skip editable row
    if (lastReadOnlyRow < 0)
        return 0;
    QString filename = static_cast<ConsoleItem *>(rootItem()->childAt(lastReadOnlyRow))->file();
    const int pos = filename.lastIndexOf('/');
    if (pos != -1)
        filename = filename.mid(pos + 1);

    QFontMetrics fm(font);
    m_maxSizeOfFileName = qMax(m_maxSizeOfFileName, fm.horizontalAdvance(filename));

    return m_maxSizeOfFileName;
}

int ConsoleItemModel::sizeOfLineNumber(const QFont &font)
{
    QFontMetrics fm(font);
    return fm.horizontalAdvance("88888");
}

#ifdef WITH_TESTS

class ConsoleItemModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheRowsAreReadableByName()
    {
        ConsoleItemModel model;
        const QHash<int, QByteArray> names = model.roleNames();

        // Qt's own two, which the stock delegates bind to.
        QCOMPARE(names.value(Qt::DisplayRole), QByteArray("display"));
        QCOMPARE(names.value(Qt::DecorationRole), QByteArray("decoration"));

        QCOMPARE(names.value(ConsoleItem::TypeRole), QByteArray("itemType"));
        QCOMPARE(names.value(ConsoleItem::FileRole), QByteArray("file"));
        QCOMPARE(names.value(ConsoleItem::FileNameRole), QByteArray("fileName"));
        QCOMPARE(names.value(ConsoleItem::LineRole), QByteArray("line"));
        QCOMPARE(names.value(ConsoleItem::ExpressionRole), QByteArray("expression"));
        QCOMPARE(names.value(ConsoleItem::TextColorRole), QByteArray("textColor"));
    }

    void testWhatARowSaysAboutItself()
    {
        ConsoleItem item(ConsoleItem::WarningType, "something happened",
                         "file.qml", 12);

        QCOMPARE(item.data(0, Qt::DisplayRole).toString(), QString("something happened"));
        QCOMPARE(item.data(0, ConsoleItem::FileRole).toString(), QString("file.qml"));
        QCOMPARE(item.data(0, ConsoleItem::LineRole).toInt(), 12);
        QCOMPARE(item.data(0, ConsoleItem::TypeRole).toInt(), int(ConsoleItem::WarningType));

        // Only the first column says anything: the tree has one, and asking a
        // second used to be how a delegate got an empty string by accident.
        QVERIFY(!item.data(1, Qt::DisplayRole).isValid());
    }

    void testTheFileARowPointsAtAndTheFileItShows()
    {
        // Two different things. The row opens the file it points at, and shows
        // only the last part of it: a path in the middle of a console message
        // is width the message needs more than the reader does.
        QCOMPARE(ConsoleItem::shownFileName("file:///tmp/some/thing.qml"), QString("thing.qml"));
        QCOMPARE(ConsoleItem::shownFileName("/tmp/some/thing.qml"), QString("thing.qml"));
        QCOMPARE(ConsoleItem::shownFileName("thing.qml"), QString("thing.qml"));
        QCOMPARE(ConsoleItem::shownFileName({}), QString());

        // And the row answers both, under different names.
        const ConsoleItem item(ConsoleItem::WarningType, "expr", "file:///tmp/some/thing.qml", 3);
        QCOMPARE(item.data(0, ConsoleItem::FileRole).toString(),
                 QString("file:///tmp/some/thing.qml"));
        QCOMPARE(item.data(0, ConsoleItem::FileNameRole).toString(), QString("thing.qml"));
    }

    void testEachKindOfMessageLooksLikeItself()
    {
        // The delegate worked this out while painting. Three kinds are told
        // apart by colour *and* by an icon, and a plain result is neither -
        // it is an answer, not a message about one.
        const QColor warning = ConsoleItem::colorForType(ConsoleItem::WarningType);
        const QColor error = ConsoleItem::colorForType(ConsoleItem::ErrorType);
        const QColor debug = ConsoleItem::colorForType(ConsoleItem::DebugType);
        const QColor plain = ConsoleItem::colorForType(ConsoleItem::DefaultType);

        QVERIFY(warning.isValid() && error.isValid() && debug.isValid() && plain.isValid());
        QVERIFY2(warning != error, "a warning is written like an error");
        QVERIFY2(debug != warning, "a message is written like a warning");

        QVERIFY(!ConsoleItem::iconForType(ConsoleItem::WarningType).isNull());
        QVERIFY(!ConsoleItem::iconForType(ConsoleItem::ErrorType).isNull());
        QVERIFY(!ConsoleItem::iconForType(ConsoleItem::InputType).isNull());
        QVERIFY2(ConsoleItem::iconForType(ConsoleItem::DefaultType).isNull(),
                 "a plain result was marked as a kind of message");

        // And a row answers with them, rather than only the free functions
        // knowing.
        const ConsoleItem item(ConsoleItem::ErrorType, "boom");
        QCOMPARE(item.data(0, ConsoleItem::TextColorRole).value<QColor>(), error);
        QVERIFY(!item.data(0, Qt::DecorationRole).value<QIcon>().isNull());
    }
};

QObject *createConsoleItemModelTest()
{
    return new ConsoleItemModelTest;
}

#endif // WITH_TESTS

} // Debugger::Internal

#ifdef WITH_TESTS
#include "consoleitemmodel.moc"
#endif

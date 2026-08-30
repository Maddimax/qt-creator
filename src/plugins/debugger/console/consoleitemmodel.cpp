// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "consoleitemmodel.h"

#include "consolehistory.h"

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <utils/aspectpresentation.h>

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
    return Utils::AspectTable::withRoleNames(names);
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

    void testWalkingThroughWhatWasTypedBefore()
    {
        // A console alternates between what was typed and what came back, and
        // the last row is the prompt. Up and Down move between the typed rows
        // only, which is the whole of what Up and Down did inside the editor.
        ConsoleItemModel model;
        model.clear();
        model.appendItem(new ConsoleItem(ConsoleItem::InputType, "1 + 1"), 0);
        model.appendItem(new ConsoleItem(ConsoleItem::DefaultType, "2"), 1);
        model.appendItem(new ConsoleItem(ConsoleItem::InputType, "foo()"), 2);
        model.appendItem(new ConsoleItem(ConsoleItem::ErrorType, "not a function"), 3);

        // State the fixture rather than trusting it: the walk is only
        // interesting if there is something in between to walk over.
        QCOMPARE(model.rowCount(), 5);
        const auto typeOf = [&model](int row) {
            return model.data(model.index(row, 0), ConsoleItem::TypeRole).toInt();
        };
        QCOMPARE(typeOf(0), int(ConsoleItem::InputType));
        QCOMPARE(typeOf(1), int(ConsoleItem::DefaultType));
        QCOMPARE(typeOf(2), int(ConsoleItem::InputType));
        QCOMPARE(typeOf(3), int(ConsoleItem::ErrorType));
        QCOMPARE(typeOf(4), int(ConsoleItem::InputType));

        // Up from the prompt reaches the last thing that was run, not the
        // error it printed.
        QCOMPARE(ConsoleItem::previousInputRow(&model, 4), 2);
        QCOMPARE(ConsoleItem::previousInputRow(&model, 2), 0);
        QCOMPARE(ConsoleItem::nextInputRow(&model, 0), 2);
        QCOMPARE(ConsoleItem::nextInputRow(&model, 2), 4);

        // The ends: pressing Up at the oldest entry, or Down at the prompt,
        // leaves what is being typed alone.
        QCOMPARE(ConsoleItem::previousInputRow(&model, 0), -1);
        QCOMPARE(ConsoleItem::nextInputRow(&model, 4), -1);

        // Asking from a row that is not itself an entry still works, and a
        // model that is not there answers rather than walking off it.
        QCOMPARE(ConsoleItem::previousInputRow(&model, 3), 2);
        QCOMPARE(ConsoleItem::nextInputRow(&model, 1), 2);
        QCOMPARE(ConsoleItem::previousInputRow(nullptr, 4), -1);
        QCOMPARE(ConsoleItem::nextInputRow(nullptr, 0), -1);
    }

    void testTypingOverTheHistoryAndBackOut()
    {
        ConsoleItemModel model;
        model.clear();
        model.appendItem(new ConsoleItem(ConsoleItem::InputType, "1 + 1"), 0);
        model.appendItem(new ConsoleItem(ConsoleItem::DefaultType, "2"), 1);
        model.appendItem(new ConsoleItem(ConsoleItem::InputType, "foo()"), 2);
        QCOMPARE(model.rowCount(), 4); // row 3 is the prompt

        ConsoleHistory history;
        history.setModel(&model);
        history.restart(3);

        QCOMPARE(history.up("half").value_or(QString()), QString("foo()"));
        QCOMPARE(history.up("foo()").value_or(QString()), QString("1 + 1"));
        QVERIFY2(!history.up("1 + 1").has_value(), "walked off the top of the history");

        QCOMPARE(history.down("1 + 1").value_or(QString()), QString("foo()"));
        // Back at the bottom is what was being typed when the walk started,
        // not the newest entry - the reader gets their unfinished line back.
        QCOMPARE(history.down("foo()").value_or(QString()), QString("half"));
        QVERIFY2(!history.down("half").has_value(), "walked off the bottom of the history");

        // Running something moves the prompt, and the walk starts there again
        // rather than from wherever it had got to.
        model.appendItem(new ConsoleItem(ConsoleItem::InputType, "bar()"), 3);
        history.restart(4);
        QCOMPARE(history.up("").value_or(QString()), QString("bar()"));
    }

    void testARowStillToBeFilledInSaysItCanBeOpened()
    {
        ConsoleItemModel model;
        model.clear();
        model.setCanFetchMore(true);
        model.appendItem(new ConsoleItem(ConsoleItem::DefaultType, "anObject",
                                         [](ConsoleItem *item) {
                                             item->appendChild(new ConsoleItem(
                                                 ConsoleItem::DefaultType, "x: 1"));
                                         }),
                         0);
        model.appendItem(new ConsoleItem(ConsoleItem::DebugType, "just a message"), 1);

        const QModelIndex lazy = model.index(0, 0);
        QCOMPARE(model.rowCount(lazy), 0);
        QVERIFY2(model.canFetchMore(lazy), "fixture: the row has something left to fetch");

        // TreeItem::hasChildren() already counts what can still be fetched, so
        // no override is needed here - but a Qt Quick view will not offer to
        // open, or even fetch, a row that answers no, and nothing else in this
        // model would notice if that stopped being true.
        QVERIFY2(model.hasChildren(lazy), "an object whose properties have not been "
                                          "fetched yet cannot be opened");
        QVERIFY2(!model.hasChildren(model.index(1, 0)), "a plain message offers to open");
    }

    void testOnlyTheRowAtTheBottomCanBeTypedInto()
    {
        ConsoleItemModel model;
        model.clear();
        model.appendItem(new ConsoleItem(ConsoleItem::InputType, "1 + 1"), 0);
        QCOMPARE(model.rowCount(), 2); // row 1 is the prompt

        // Which row is the prompt is in flags(), and QML cannot read flags.
        QCOMPARE(model.roleNames().value(Utils::AspectTable::EditableRole),
                 QByteArray("editable"));
        QVERIFY(model.data(model.index(1, 0), Utils::AspectTable::EditableRole).toBool());
        QVERIFY2(!model.data(model.index(0, 0), Utils::AspectTable::EditableRole).toBool(),
                 "an entry that has already been run is still offered as the prompt");
    }

    void testWhatCopyingARowPutsOnTheClipboard()
    {
        QCOMPARE(ConsoleItem::copiedText("foo()", "file:///tmp/a.qml", 7),
                 QString("foo() /tmp/a.qml: 7"));
        QCOMPARE(ConsoleItem::copiedText("foo()", "/tmp/a.qml", 7),
                 QString("foo() /tmp/a.qml: 7"));
        // A message with nothing behind it copies as itself, with no room left
        // for a stray separator.
        QCOMPARE(ConsoleItem::copiedText("just a message", {}, -1), QString("just a message"));
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

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bookmarkslist.h"

#include "bookmark.h"
#include "bookmarkmanager.h"
#include "texteditorconstants.h"
#include "texteditortr.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>

#include <utils/checkablemessagebox.h>
#include <utils/qtcassert.h>
#include <utils/storekey.h>

#include <QDrag>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMimeData>

namespace TextEditor::Internal {

void removeAllBookmarksAsked()
{
    if (Utils::CheckableMessageBox::question(
            Tr::tr("Remove All Bookmarks"),
            Tr::tr("Are you sure you want to remove all bookmarks from "
                   "all files in the current session?"),
            Utils::Key("RemoveAllBookmarks"))
        != QMessageBox::Yes) {
        return;
    }

    while (bookmarkManager().rowCount()) {
        Bookmark * const mark
            = bookmarkManager().bookmarkForIndex(bookmarkManager().index(0, 0));
        QTC_ASSERT(mark, break);
        bookmarkManager().deleteBookmark(mark);
    }
}

void fillBookmarksContextMenu(QMenu *menu, int row)
{
    Core::Command * const moveUp
        = Core::ActionManager::command(Constants::BOOKMARKS_MOVEUP_ACTION);
    Core::Command * const moveDown
        = Core::ActionManager::command(Constants::BOOKMARKS_MOVEDOWN_ACTION);
    Core::Command * const sortByFilenames
        = Core::ActionManager::command(Constants::BOOKMARKS_SORTBYFILENAMES_ACTION);
    QTC_ASSERT(moveUp && moveDown && sortByFilenames, return);

    menu->addAction(moveUp->action());
    menu->addAction(moveDown->action());
    menu->addSeparator();
    menu->addAction(sortByFilenames->action());
    menu->addSeparator();
    QAction * const edit = menu->addAction(Tr::tr("&Edit"));
    menu->addSeparator();
    QAction * const remove = menu->addAction(Tr::tr("&Remove"));
    menu->addSeparator();
    QAction * const removeAll = menu->addAction(Tr::tr("Remove All"));

    // With nothing under the pointer only the two entries about the list as a
    // whole are left. Edit in particular has to go: BookmarkManager::edit()
    // indexes its list with the current row and does not guard it.
    if (row < 0 || row >= bookmarkManager().rowCount()) {
        moveUp->action()->setEnabled(false);
        moveDown->action()->setEnabled(false);
        edit->setEnabled(false);
        remove->setEnabled(false);
    }
    removeAll->setEnabled(bookmarkManager().rowCount() > 0);

    QObject::connect(edit, &QAction::triggered, menu, [] { bookmarkManager().edit(); });
    QObject::connect(remove, &QAction::triggered, menu, [row] {
        if (Bookmark * const mark
            = bookmarkManager().bookmarkForIndex(bookmarkManager().index(row, 0))) {
            bookmarkManager().deleteBookmark(mark);
        }
    });
    QObject::connect(removeAll, &QAction::triggered, menu, [] { removeAllBookmarksAsked(); });
}

BookmarksList::BookmarksList(QObject *parent)
    : QObject(parent)
{
    connect(bookmarkManager().selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &BookmarksList::currentRowChanged);
}

BookmarksList::~BookmarksList() = default;

QAbstractItemModel *BookmarksList::model() const
{
    return &bookmarkManager();
}

int BookmarksList::currentRow() const
{
    return bookmarkManager().selectionModel()->currentIndex().row();
}

void BookmarksList::setCurrentRow(int row)
{
    const QModelIndex index = bookmarkManager().index(row, 0);
    if (index == bookmarkManager().selectionModel()->currentIndex())
        return;
    bookmarkManager().selectionModel()->setCurrentIndex(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void BookmarksList::activate(int row)
{
    Bookmark * const mark = bookmarkManager().bookmarkForIndex(bookmarkManager().index(row, 0));
    if (mark && !bookmarkManager().gotoBookmark(mark))
        bookmarkManager().deleteBookmark(mark);
}

void BookmarksList::remove(int row)
{
    if (Bookmark * const mark
        = bookmarkManager().bookmarkForIndex(bookmarkManager().index(row, 0))) {
        bookmarkManager().deleteBookmark(mark);
    }
}

QMimeData *BookmarksList::dragMimeData(int row) const
{
    // index() creates an index for any row it is given, so the bound is
    // checked here rather than read off the index.
    if (row < 0 || row >= bookmarkManager().rowCount())
        return nullptr;
    return bookmarkManager().mimeData({bookmarkManager().index(row, 0)});
}

void BookmarksList::startDrag(int row)
{
    QMimeData * const data = dragMimeData(row);
    if (!data)
        return;

    // The row a drag starts on is the row it is about: the widget view gets
    // that from the item view's own press, and BookmarkManager::move() reads
    // the current index to say which rows a reorder changed.
    setCurrentRow(row);

    // The drag belongs to the view being dragged from, and it is what the
    // reader is holding until they let go: exec() blocks, as it does for the
    // item view.
    auto * const drag = new QDrag(this);
    drag->setMimeData(data);
    drag->exec(Qt::MoveAction);
}

bool BookmarksList::dropRowOn(int draggedRow, int targetRow)
{
    const std::unique_ptr<QMimeData> data(dragMimeData(draggedRow));
    if (!data)
        return false;

    const QModelIndex target = bookmarkManager().index(targetRow, 0);
    if (!bookmarkManager().canDropMimeData(data.get(), Qt::MoveAction, -1, -1, target))
        return false;
    return bookmarkManager().dropMimeData(data.get(), Qt::MoveAction, -1, -1, target);
}

QObjectList BookmarksList::contextMenuActions(int row)
{
    m_menu = std::make_unique<QMenu>();
    fillBookmarksContextMenu(m_menu.get(), row);
    QObjectList actions;
    for (QAction * const action : m_menu->actions())
        actions << action;
    return actions;
}

} // namespace TextEditor::Internal

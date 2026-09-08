// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bookmarkslist.h"

#include "bookmark.h"
#include "bookmarkmanager.h"

#include <QItemSelectionModel>

namespace TextEditor::Internal {

BookmarksList::BookmarksList(QObject *parent)
    : QObject(parent)
{
    connect(bookmarkManager().selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &BookmarksList::currentRowChanged);
}

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

void BookmarksList::removeAll()
{
    bookmarkManager().removeAllBookmarks();
}

} // namespace TextEditor::Internal

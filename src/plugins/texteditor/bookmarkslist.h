// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
QT_END_NAMESPACE

namespace TextEditor::Internal {

// What the Qt Quick Bookmarks view needs of the bookmark manager. The widget
// view is a QListView over the manager itself and reaches into it from its
// delegate and its event handlers; a QML delegate cannot, so this stands
// between them.
//
// The row a reader is on is the *manager's* - the widget view shares its
// selection model rather than keeping one, because the Previous and Next
// commands move that selection from outside the view. So this reads and
// writes that same selection model, and a QML list that kept a selection of
// its own would disagree with the toolbar buttons above it.
class BookmarksList : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)

public:
    explicit BookmarksList(QObject *parent = nullptr);

    QAbstractItemModel *model() const;
    int currentRow() const;
    void setCurrentRow(int row);

    // Opening a bookmark that is no longer there deletes it, which is what
    // the widget view does with whatever gotoBookmark() could not reach.
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void removeAll();

signals:
    void currentRowChanged();
};

} // namespace TextEditor::Internal

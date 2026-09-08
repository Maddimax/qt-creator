// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

#include <memory>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
class QMenu;
QT_END_NAMESPACE

namespace TextEditor::Internal {

// The Bookmarks right-click menu, filled for whichever view is drawing it, so
// that the widget view and the Qt Quick pane cannot drift apart. row is the
// row the pointer is over, or -1 where there is none.
//
// Three of the six entries - Move Up, Move Down and Edit - act on the
// manager's current row rather than on the row passed in. A view has to have
// made the clicked row current before asking for this menu; the widget view
// gets that from the item view's own right-press.
void fillBookmarksContextMenu(QMenu *menu, int row);

// Removing every bookmark asks first, with a "Do not ask again" box, and does
// it one at a time - BookmarkManager::removeAllBookmarks() neither saves the
// session nor updates the actions, because its caller is the session load.
void removeAllBookmarksAsked();

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
    // Where the QMenu below is destroyed, which needs it to be more than the
    // forward declaration this header has.
    ~BookmarksList() override;

    QAbstractItemModel *model() const;
    int currentRow() const;
    void setCurrentRow(int row);

    // Opening a bookmark that is no longer there deletes it, which is what
    // the widget view does with whatever gotoBookmark() could not reach.
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void remove(int row);

    // What the right-click menu holds, in the order the widget view holds it,
    // and with the same entries disabled. The QActions belong to a menu kept
    // alive here while they are on screen; a separator arrives as an entry
    // with no text, which is what the QML draws a line for.
    Q_INVOKABLE QObjectList contextMenuActions(int row);

signals:
    void currentRowChanged();

private:
    // Owns the QActions contextMenuActions() hands out. They are built for
    // one row and are worthless once another row is asked about.
    std::unique_ptr<QMenu> m_menu;
};

} // namespace TextEditor::Internal

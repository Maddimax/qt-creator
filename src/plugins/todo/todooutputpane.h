// Copyright (C) 2016 Dmitry Savchenko
// Copyright (C) 2016 Vasiliy Sorokin
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "settings.h"

#include <coreplugin/ioutputpane.h>

#include <QPointer>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
class QActionGroup;
class QModelIndex;
class QSortFilterProxyModel;
QT_END_NAMESPACE

namespace Todo::Internal {

class TodoItem;
class TodoItemsModel;

class TodoOutputPane : public Core::IOutputPane
{
    Q_OBJECT

    // What the view binds to. Plain accessors are invisible to QML, and an
    // untyped pane is invisible to qmllint, so a missing one is a blank list
    // and no diagnostic anywhere.
    Q_PROPERTY(QAbstractItemModel *rows READ rows CONSTANT)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged)

public:
    TodoOutputPane(TodoItemsModel *todoItemsModel, QObject *parent);
    ~TodoOutputPane() override;

    QWidget *outputWidget(QWidget *parent) override;
    QList<ToolBarItem> toolBarItems() const override;
    void clearContents() override;
    void setFocus() override;
    bool hasFocus() const override;
    bool canFocus() const override;
    bool canNavigate() const override;
    bool canNext() const override;
    bool canPrevious() const override;
    void goToNext() override;
    void goToPrev() override;

    void setScanningScope(ScanningScope scanningScope);

    // What the list shows, filtered, and which of its rows is current. The
    // current row used to be the tree view's selection, so the pane could not
    // say where it was without one.
    QAbstractItemModel *rows() const;
    int currentRow() const { return m_currentRow; }
    Q_INVOKABLE void setCurrentRow(int row);
    // A row was chosen: open what it points at.
    Q_INVOKABLE void activateRow(int row);
    // Which column the list is ordered by, which the header reports and the
    // view used to keep for itself.
    Q_INVOKABLE void sortBy(int column, bool ascending);

signals:
    void currentRowChanged();

private:
    void todoItemClicked(const TodoItem &item);
    void scanningScopeChanged(ScanningScope scanningScope);
    void scopeActionTriggered(QAction *action);
    void todoTreeViewClicked(const QModelIndex &index);
    void updateTodoCount();
    void updateKeywordFilter();
    void clearKeywordFilter();

    void createTreeView();
    void freeTreeView();
    void createToolBarItems();
    void freeToolBarItems();

    QModelIndex selectedModelIndex();
    int nextRow() const;
    int previousRow() const;

    // What draws the list. A QQuickWidget, built on demand and owned by
    // whoever asked for it.
    QPointer<QWidget> m_view;
    int m_currentRow = -1;
    // Which column the list is ordered by. The tree view kept this in its
    // header, so nothing could say what the order was without one.
    int m_sortColumn = 0;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    QAction *m_currentFileAction;
    QAction *m_wholeProjectAction;
    QAction *m_subProjectAction;
    QWidget *m_spacer;
    QActionGroup *m_scopeActions;
    QList<TodoItem> *items;
    TodoItemsModel *m_todoItemsModel;
    QSortFilterProxyModel *m_filteredTodoItemsModel;
    QList<QAction *> m_filterActions;
};

TodoOutputPane &todoOutputPane();

void setupTodoOutputPane(QObject *guard);

#ifdef WITH_TESTS
QObject *createTodoPaneTest();
#endif

} // Todo::Internal

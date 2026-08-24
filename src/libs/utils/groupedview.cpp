// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "groupedview.h"

#include "guiutils.h"
#include "qtcassert.h"
#include "utilstr.h"

#include <QAction>
#include <QHeaderView>
#include <QItemSelectionModel>

namespace Utils {

GroupedView::GroupedView(GroupedModel &model)
    : m_model(model)
    , m_selection(model)
{
    m_view.setRootIsDecorated(false);
    m_view.setExpandsOnDoubleClick(false);
    m_view.setModel(model.groupedDisplayModel());
    m_view.setUniformRowHeights(true);
    m_view.setSelectionMode(QAbstractItemView::SingleSelection);
    m_view.setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view.setSortingEnabled(true);
    m_view.sortByColumn(0, Qt::AscendingOrder);
    m_view.expandAll();

    QHeaderView *header = m_view.header();
    header->setStretchLastSection(true);
    for (int i = 0; i < model.columnCount() - 1; ++i)
        header->setSectionResizeMode(i, QHeaderView::ResizeToContents);

    connect(model.groupedDisplayModel(), &QAbstractItemModel::modelReset, this, [this] {
        m_view.expandAll();
    });

    // The tree and the selection each have a notion of what is current, and
    // either can be the one that changed it.
    connect(m_view.selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &current) {
        if (m_syncing)
            return;
        m_syncing = true;
        m_selection.setCurrentRow(m_model.mapToSource(current).row());
        m_syncing = false;
    });
    connect(&m_selection, &GroupedSelection::currentRowChanged,
            this, [this](int oldRow, int newRow) {
        if (!m_syncing) {
            m_syncing = true;
            selectRow(newRow);
            m_syncing = false;
        }
        updateButtons();
        emit currentRowChanged(oldRow, newRow);
    });
    connect(&m_selection, &GroupedSelection::actionsChanged, this, &GroupedView::updateButtons);
    connect(&m_selection, &GroupedSelection::currentRemoved, this, &GroupedView::currentRemoved);
    connect(&m_selection, &GroupedSelection::currentCloned, this, &GroupedView::currentCloned);

    m_cloneButton.setText(Tr::tr("Clone"));
    m_cloneButton.setEnabled(false);
    connect(&m_cloneButton, &QPushButton::clicked, this, &GroupedView::cloneCurrent);

    m_makeDefaultButton.setText(Tr::tr("Make Default"));
    m_makeDefaultButton.setEnabled(false);
    connect(&m_makeDefaultButton, &QPushButton::clicked,
            this, [this] { m_selection.makeCurrentDefault(); });

    m_removeButton.setText(Tr::tr("Remove"));
    m_removeButton.setEnabled(false);
    connect(&m_removeButton, &QPushButton::clicked, this, &GroupedView::removeCurrent);

    auto removeAction = new QAction(&m_view);
    removeAction->setShortcut(QKeySequence::Delete);
    removeAction->setShortcutContext(Qt::WidgetShortcut);
    m_view.addAction(removeAction);
    connect(removeAction, &QAction::triggered, this, [this] {
        if (m_removeButton.isEnabled())
            removeCurrent();
    });
}

QTreeView &GroupedView::view()
{
    return m_view;
}

QPushButton &GroupedView::cloneButton()
{
    return m_cloneButton;
}

QPushButton &GroupedView::removeButton()
{
    return m_removeButton;
}

QPushButton &GroupedView::makeDefaultButton()
{
    return m_makeDefaultButton;
}

void GroupedView::setCanRemoveRow(std::function<bool(int)> predicate)
{
    m_selection.setCanRemoveRow(std::move(predicate));
}

void GroupedView::setCanCloneRow(std::function<bool(int)> predicate)
{
    m_selection.setCanCloneRow(std::move(predicate));
}

void GroupedView::updateButtons()
{
    m_removeButton.setText(m_selection.currentIsRemoved() ? Tr::tr("Restore") : Tr::tr("Remove"));
    m_removeButton.setEnabled(m_selection.canRemoveCurrent());
    m_cloneButton.setEnabled(m_selection.canCloneCurrent());
    m_makeDefaultButton.setEnabled(m_selection.canMakeCurrentDefault());
}

int GroupedView::currentRow() const
{
    return m_selection.currentRow();
}

void GroupedView::selectRow(int row)
{
    const QModelIndex idx = row >= 0 ? m_model.mapFromSource(m_model.index(row, 0)) : QModelIndex{};
    m_view.selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
    m_view.selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect
                                      | QItemSelectionModel::Rows);
    m_selection.setCurrentRow(row);
}

void GroupedView::scrollToRow(int row)
{
    if (row >= 0)
        m_view.scrollTo(m_model.mapFromSource(m_model.index(row, 0)));
}

void GroupedView::cloneCurrent()
{
    m_selection.cloneCurrent();
}

void GroupedView::removeCurrent()
{
    m_selection.removeCurrent();
}

} // namespace Utils

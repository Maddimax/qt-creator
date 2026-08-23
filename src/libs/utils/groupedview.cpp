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

    connect(model.groupedDisplayModel(), &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        const int row = currentRow();
        m_savedVariant = row >= 0 ? m_model.volatileVariant(row) : QVariant{};
    });

    connect(model.groupedDisplayModel(), &QAbstractItemModel::modelReset, this, [this] {
        m_view.expandAll();
        if (m_savedVariant.isValid()) {
            for (int row = 0; row < m_model.itemCount(); ++row) {
                if (m_model.volatileVariant(row) == m_savedVariant) {
                    selectRow(row);
                    return;
                }
            }
        }
        emit currentRowChanged(-1, -1);
    });

    connect(m_view.selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](QItemSelection selected, QItemSelection deselected) {
        const QModelIndex previous = deselected.isEmpty() ? QModelIndex{}
                                                          : deselected.indexes().first();
        const QModelIndex current = selected.isEmpty() ? QModelIndex{}
                                                       : selected.indexes().first();
        emit currentRowChanged(m_model.mapToSource(previous).row(),
                               m_model.mapToSource(current).row());
        updateButtons();
    });

    m_cloneButton.setText(Tr::tr("Clone"));
    m_cloneButton.setEnabled(false);
    connect(&m_cloneButton, &QPushButton::clicked, this, &GroupedView::cloneCurrent);

    m_makeDefaultButton.setText(Tr::tr("Make Default"));
    m_makeDefaultButton.setEnabled(false);
    connect(&m_makeDefaultButton, &QPushButton::clicked, this, [this] {
        m_removedDefaultRow = -1;
        m_model.setVolatileDefaultRow(currentRow());
        updateButtons();
    });

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
    m_canRemove = std::move(predicate);
}

void GroupedView::setCanCloneRow(std::function<bool(int)> predicate)
{
    m_canClone = std::move(predicate);
}

void GroupedView::updateButtons()
{
    const int row = currentRow();
    const bool isRemoved = row >= 0 && m_model.isRemoved(row);
    m_removeButton.setText(isRemoved ? Tr::tr("Restore") : Tr::tr("Remove"));
    const bool canRemove = row >= 0 && (isRemoved || !m_canRemove || m_canRemove(row));
    m_removeButton.setEnabled(canRemove);
    const bool canClone = row >= 0 && !isRemoved && (!m_canClone || m_canClone(row));
    m_cloneButton.setEnabled(canClone);
    const bool canMakeDefault = row >= 0 && !isRemoved && !m_model.isDefault(row);
    m_makeDefaultButton.setEnabled(canMakeDefault);
}

int GroupedView::currentRow() const
{
    return m_model.mapToSource(m_view.selectionModel()->currentIndex()).row();
}

void GroupedView::selectRow(int row)
{
    const QModelIndex idx = row >= 0 ? m_model.mapFromSource(m_model.index(row, 0)) : QModelIndex{};
    m_view.selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
    m_view.selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect
                                      | QItemSelectionModel::Rows);
}

void GroupedView::scrollToRow(int row)
{
    if (row >= 0)
        m_view.scrollTo(m_model.mapFromSource(m_model.index(row, 0)));
}

void GroupedView::cloneCurrent()
{
    const int row = currentRow();
    QTC_ASSERT(row >= 0, return);
    const int newRow = m_model.cloneRow(row);
    if (newRow >= 0) {
        selectRow(newRow);
        emit currentCloned();
    }
}

QModelIndex GroupedView::indexAfterRemoval(const QModelIndex &current) const
{
    const QAbstractItemModel *model = m_view.model();
    const int currentGroup = current.parent().row();
    QTC_ASSERT(currentGroup >= 0, return {});

    const auto isKept = [this](const QModelIndex &index) {
        const int row = m_model.mapToSource(index).row();
        return row >= 0 && !m_model.isRemoved(row);
    };

    // Prefer staying in the same group: the next kept item there, else the
    // previous one.
    const QModelIndex currentParent = model->index(currentGroup, 0);
    for (int row = current.row() + 1, m = model->rowCount(currentParent); row < m; ++row) {
        const QModelIndex index = model->index(row, 0, currentParent);
        if (isKept(index))
            return index;
    }
    for (int row = current.row() - 1; row >= 0; --row) {
        const QModelIndex index = model->index(row, 0, currentParent);
        if (isKept(index))
            return index;
    }

    // The group has no other item left; move to the next group in view order,
    // and fall back to a previous one.
    for (int group = currentGroup + 1, n = model->rowCount(); group < n; ++group) {
        const QModelIndex parent = model->index(group, 0);
        for (int row = 0, m = model->rowCount(parent); row < m; ++row) {
            const QModelIndex index = model->index(row, 0, parent);
            if (isKept(index))
                return index;
        }
    }
    for (int group = currentGroup - 1; group >= 0; --group) {
        const QModelIndex parent = model->index(group, 0);
        for (int row = model->rowCount(parent) - 1; row >= 0; --row) {
            const QModelIndex index = model->index(row, 0, parent);
            if (isKept(index))
                return index;
        }
    }
    return {};
}

void GroupedView::removeCurrent()
{
    const int row = currentRow();
    QTC_ASSERT(row >= 0, return);
    const bool isRestoring = m_model.isRemoved(row);
    if (!isRestoring && m_model.isDefault(row))
        m_removedDefaultRow = row;
    // Removal keeps the item in the list, so move on to make repeated removal work.
    // The index is persistent as removing an added item does shift the rows below.
    const QPersistentModelIndex next
        = isRestoring ? QModelIndex{}
                      : m_model.mapToSource(indexAfterRemoval(m_view.currentIndex()));
    m_model.markRemoved(row);
    if (isRestoring && m_removedDefaultRow == row) {
        m_model.setVolatileDefaultRow(row);
        m_removedDefaultRow = -1;
    }
    if (next.isValid())
        selectRow(next.row());
    updateButtons();
    emit currentRemoved();
}

} // namespace Utils

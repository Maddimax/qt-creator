// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "groupedselection.h"

#include "groupedmodel.h"
#include "qtcassert.h"

namespace Utils {

GroupedSelection::GroupedSelection(GroupedModel &model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    // A reset rebuilds the tree, so the row that was current is gone. What was
    // in it is not, so it is found again by value.
    connect(model.groupedDisplayModel(), &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        m_savedVariant = m_currentRow >= 0 ? m_model.volatileVariant(m_currentRow) : QVariant{};
    });
    connect(model.groupedDisplayModel(), &QAbstractItemModel::modelReset, this, [this] {
        if (m_savedVariant.isValid()) {
            for (int row = 0; row < m_model.itemCount(); ++row) {
                if (m_model.volatileVariant(row) == m_savedVariant) {
                    setCurrentRow(row);
                    return;
                }
            }
        }
        m_currentRow = -1;
        emit currentRowChanged(-1, -1);
        emit actionsChanged();
    });
}

GroupedModel &GroupedSelection::model() const
{
    return m_model;
}

int GroupedSelection::currentRow() const
{
    return m_currentRow;
}

void GroupedSelection::setCurrentRow(int row)
{
    if (row == m_currentRow)
        return;
    const int oldRow = m_currentRow;
    m_currentRow = row;
    emit currentRowChanged(oldRow, row);
    emit actionsChanged();
}

void GroupedSelection::setCanRemoveRow(std::function<bool(int)> predicate)
{
    m_canRemove = std::move(predicate);
    emit actionsChanged();
}

void GroupedSelection::setCanCloneRow(std::function<bool(int)> predicate)
{
    m_canClone = std::move(predicate);
    emit actionsChanged();
}

bool GroupedSelection::currentIsRemoved() const
{
    return m_currentRow >= 0 && m_model.isRemoved(m_currentRow);
}

bool GroupedSelection::canRemoveCurrent() const
{
    if (m_currentRow < 0)
        return false;
    // What is already on its way out can always be brought back, whatever the
    // page says about removing it.
    return currentIsRemoved() || !m_canRemove || m_canRemove(m_currentRow);
}

bool GroupedSelection::canCloneCurrent() const
{
    if (m_currentRow < 0 || currentIsRemoved())
        return false;
    return !m_canClone || m_canClone(m_currentRow);
}

bool GroupedSelection::canMakeCurrentDefault() const
{
    return m_currentRow >= 0 && !currentIsRemoved() && !m_model.isDefault(m_currentRow);
}

void GroupedSelection::makeCurrentDefault()
{
    QTC_ASSERT(m_currentRow >= 0, return);
    m_removedDefaultRow = -1;
    m_model.setVolatileDefaultRow(m_currentRow);
    emit actionsChanged();
}

void GroupedSelection::cloneCurrent()
{
    QTC_ASSERT(m_currentRow >= 0, return);
    const int newRow = m_model.cloneRow(m_currentRow);
    if (newRow >= 0) {
        setCurrentRow(newRow);
        emit currentCloned();
    }
}

// Which row to look at once the current one is on its way out. Removal keeps
// the item in the list, so staying put would make removing several in a row
// do nothing after the first.
int GroupedSelection::rowAfterRemoval() const
{
    const QAbstractItemModel *display = m_model.groupedDisplayModel();
    const QModelIndex current = m_model.mapFromSource(m_model.index(m_currentRow, 0));
    const int currentGroup = current.parent().row();
    QTC_ASSERT(currentGroup >= 0, return -1);

    const auto keptRow = [this](const QModelIndex &index) {
        const int row = m_model.mapToSource(index).row();
        return row >= 0 && !m_model.isRemoved(row) ? row : -1;
    };

    // Prefer staying in the same group: the next kept item there, else the
    // previous one.
    const QModelIndex currentParent = display->index(currentGroup, 0);
    for (int row = current.row() + 1, m = display->rowCount(currentParent); row < m; ++row) {
        if (const int kept = keptRow(display->index(row, 0, currentParent)); kept >= 0)
            return kept;
    }
    for (int row = current.row() - 1; row >= 0; --row) {
        if (const int kept = keptRow(display->index(row, 0, currentParent)); kept >= 0)
            return kept;
    }

    // The group has no other item left; move to the next group in view order,
    // and fall back to a previous one.
    for (int group = currentGroup + 1, n = display->rowCount(); group < n; ++group) {
        const QModelIndex parent = display->index(group, 0);
        for (int row = 0, m = display->rowCount(parent); row < m; ++row) {
            if (const int kept = keptRow(display->index(row, 0, parent)); kept >= 0)
                return kept;
        }
    }
    for (int group = currentGroup - 1; group >= 0; --group) {
        const QModelIndex parent = display->index(group, 0);
        for (int row = display->rowCount(parent) - 1; row >= 0; --row) {
            if (const int kept = keptRow(display->index(row, 0, parent)); kept >= 0)
                return kept;
        }
    }
    return -1;
}

void GroupedSelection::removeCurrent()
{
    const int row = m_currentRow;
    QTC_ASSERT(row >= 0, return);
    const bool isRestoring = m_model.isRemoved(row);
    if (!isRestoring && m_model.isDefault(row))
        m_removedDefaultRow = row;
    // Taken before the removal: removing an added item drops it from the list
    // and shifts the rows below, so the answer has to survive that.
    const QPersistentModelIndex next
        = isRestoring ? QModelIndex{}
                      : m_model.mapFromSource(m_model.index(rowAfterRemoval(), 0));
    m_model.markRemoved(row);
    if (isRestoring && m_removedDefaultRow == row) {
        m_model.setVolatileDefaultRow(row);
        m_removedDefaultRow = -1;
    }
    if (next.isValid())
        setCurrentRow(m_model.mapToSource(next).row());
    emit actionsChanged();
    emit currentRemoved();
}

} // namespace Utils

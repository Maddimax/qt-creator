// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <QObject>
#include <QPersistentModelIndex>
#include <QVariant>

#include <functional>

namespace Utils {

class GroupedModel;

// Which item of a GroupedModel is being looked at, and what may be done to it.
// None of this is a view's business - it is the same answer for a QTreeView and
// for a Qt Quick TreeView - but all of it used to live in GroupedView.
//
// Rows are the model's own, not the tree's: a view maps through
// GroupedModel::mapFromSource() to show one.
class QTCREATOR_UTILS_EXPORT GroupedSelection : public QObject
{
    Q_OBJECT

public:
    explicit GroupedSelection(GroupedModel &model, QObject *parent = nullptr);

    GroupedModel &model() const;

    int currentRow() const;
    void setCurrentRow(int row);

    // Whether a page lets this row go or be copied. Unset means any row may be.
    void setCanRemoveRow(std::function<bool(int)> predicate);
    void setCanCloneRow(std::function<bool(int)> predicate);

    bool canRemoveCurrent() const;
    bool canCloneCurrent() const;
    bool canMakeCurrentDefault() const;
    // Removal is undone rather than done again, so the button says so.
    bool currentIsRemoved() const;

    void removeCurrent();
    void cloneCurrent();
    void makeCurrentDefault();

signals:
    void currentRowChanged(int oldRow, int newRow);
    void currentRemoved();
    void currentCloned();
    // Any of the three answers above may have changed.
    void actionsChanged();

private:
    int rowAfterRemoval() const;

    GroupedModel &m_model;
    int m_currentRow = -1;
    QVariant m_savedVariant;
    int m_removedDefaultRow = -1;
    std::function<bool(int)> m_canRemove;
    std::function<bool(int)> m_canClone;
};

} // namespace Utils

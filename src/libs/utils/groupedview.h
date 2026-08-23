// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "groupedmodel.h"

#include <QPushButton>
#include <QTreeView>

#include <functional>

namespace Utils {

class QTCREATOR_UTILS_EXPORT GroupedView : public QObject
{
    Q_OBJECT

public:
    explicit GroupedView(GroupedModel &model);

    QTreeView &view();

    QPushButton &removeButton();
    QPushButton &cloneButton();
    QPushButton &makeDefaultButton();

    void setCanRemoveRow(std::function<bool(int)> predicate);
    void setCanCloneRow(std::function<bool(int)> predicate);

    int currentRow() const;
    void selectRow(int row);
    void scrollToRow(int row);

    void removeCurrent();
    void cloneCurrent();

    void updateButtons();

signals:
    void currentRowChanged(int oldRow, int newRow);
    void currentRemoved();
    void currentCloned();

private:
    QModelIndex indexAfterRemoval(const QModelIndex &current) const;

    GroupedModel &m_model;
    QTreeView m_view;
    QPushButton m_removeButton;
    QPushButton m_cloneButton;
    QPushButton m_makeDefaultButton;
    QVariant m_savedVariant;
    int m_removedDefaultRow = -1;
    std::function<bool(int)> m_canRemove;
    std::function<bool(int)> m_canClone;
};

} // namespace Utils

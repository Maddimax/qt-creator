// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "groupedmodel.h"
#include "groupedselection.h"

#include <QPushButton>
#include <QTreeView>

#include <functional>

namespace Utils {

// A QTreeView over a GroupedModel with the three buttons that act on the
// current item. What may be done to that item, and what happens when it is,
// is GroupedSelection's; this keeps the tree and the buttons in step with it.
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
    GroupedModel &m_model;
    GroupedSelection m_selection;
    QTreeView m_view;
    QPushButton m_removeButton;
    QPushButton m_cloneButton;
    QPushButton m_makeDefaultButton;
    bool m_syncing = false;
};

} // namespace Utils

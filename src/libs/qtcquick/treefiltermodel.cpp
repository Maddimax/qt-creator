// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "treefiltermodel.h"

namespace QtcQuick {

TreeFilterModel::TreeFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterKeyColumn(-1);
    setRecursiveFilteringEnabled(true);
}

bool TreeFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    // A branch is worth showing when it matches itself - a section named after
    // what was typed - and recursive filtering keeps it when a child does.
    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}

} // namespace QtcQuick

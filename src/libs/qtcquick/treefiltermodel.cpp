// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "treefiltermodel.h"

#include <utils/aspectpresentation.h>

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
    if (QSortFilterProxyModel::filterAcceptsRow(row, parent))
        return true;
    // What the row shows is not always all there is to look for: a command is
    // found by the portable text of its shortcut, which is not what the row
    // says. Same as TableFilterModel.
    const QVariant extra
        = sourceModel()->index(row, 0, parent).data(Utils::AspectTable::FilterTextRole);
    if (!extra.isValid())
        return false;
    return extra.toString().contains(filterRegularExpression());
}

} // namespace QtcQuick

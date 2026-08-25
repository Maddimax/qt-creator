// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "tablefiltermodel.h"

#include <utils/aspectpresentation.h>

namespace QtcQuick {

TableFilterModel::TableFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterKeyColumn(-1);
}

bool TableFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (QSortFilterProxyModel::filterAcceptsRow(row, parent))
        return true;
    // What the row shows is not always all there is to look for. A MIME type
    // is found by its glob patterns, and those are not a column.
    const QVariant extra
        = sourceModel()->index(row, 0, parent).data(Utils::AspectTable::FilterTextRole);
    if (!extra.isValid())
        return false;
    return extra.toString().contains(filterRegularExpression());
}

} // namespace QtcQuick

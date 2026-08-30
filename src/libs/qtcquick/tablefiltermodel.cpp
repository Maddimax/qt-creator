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

// A column of numbers sorts as numbers. Compared as text, a process list puts
// 10 before 9 and a column of sizes is nonsense - which is why every widget
// table over such a column had a comparison of its own.
bool TableFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QString l = sourceModel()->data(left).toString();
    const QString r = sourceModel()->data(right).toString();

    bool leftIsNumber = false;
    bool rightIsNumber = false;
    const qlonglong ln = l.toLongLong(&leftIsNumber);
    const qlonglong rn = r.toLongLong(&rightIsNumber);
    if (leftIsNumber && rightIsNumber)
        return ln < rn;

    // Case-insensitively, so that a list of names reads as one list rather
    // than every capital ahead of every lowercase letter.
    return QString::compare(l, r, Qt::CaseInsensitive) < 0;
}

} // namespace QtcQuick

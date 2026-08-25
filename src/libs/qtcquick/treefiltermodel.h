// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QSortFilterProxyModel>
#include <QtQmlIntegration>

namespace QtcQuick {

// A tree's rows narrowed by what was typed into the filter field. Matches
// against every column, and keeps a branch whose children match: filtering a
// tree by its top-level rows alone would hide everything that was looked for.
class QTCQUICK_EXPORT TreeFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit TreeFilterModel(QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
};

} // namespace QtcQuick

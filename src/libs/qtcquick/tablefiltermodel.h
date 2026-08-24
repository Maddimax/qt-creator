// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QSortFilterProxyModel>
#include <QtQmlIntegration>

namespace QtcQuick {

// A table's rows narrowed by what was typed into the filter field. Matches
// against every column, because what is being looked for is a row.
class QTCQUICK_EXPORT TableFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit TableFilterModel(QObject *parent = nullptr);
};

} // namespace QtcQuick

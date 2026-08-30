// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "consoleitem.h"
#include <utils/treemodel.h>

#include <QItemSelectionModel>

QT_BEGIN_NAMESPACE
class QFont;
QT_END_NAMESPACE

namespace Debugger::Internal {

class ConsoleItemModel : public Utils::TreeModel<>
{
    // QML addresses a role by name, and a TreeModel names only the roles it
    // was registered for. Without these a Qt Quick delegate finds nothing
    // under any of them, and an unanswered role is undefined rather than an
    // error - so the row draws blank and nothing says why.

    Q_OBJECT
public:
    QHash<int, QByteArray> roleNames() const override;


    explicit ConsoleItemModel(QObject *parent = nullptr);

    void shiftEditableRow();

    void appendItem(ConsoleItem *item, int position = -1);

    int sizeOfFile(const QFont &font);
    int sizeOfLineNumber(const QFont &font);

    void clear();

    void setCanFetchMore(bool canFetchMore);
    bool canFetchMore(const QModelIndex &parent) const override;

signals:
    void selectEditableRow(const QModelIndex &index, QItemSelectionModel::SelectionFlags flags);

private:
    int m_maxSizeOfFileName = 0;
    bool m_canFetchMore = false;
};

#ifdef WITH_TESTS
QObject *createConsoleItemModelTest();
#endif

} // Debugger::Internal

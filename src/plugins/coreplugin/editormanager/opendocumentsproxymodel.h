// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QAbstractProxyModel>

namespace Core::Internal {

// What both Open Documents sidebars show: DocumentModel's rows without its
// <no document> entry, each with the file's icon, the colour its version
// control state is drawn in and a tool tip naming that state. The tree view
// and the Qt Quick list share it, so neither can drift from the other.
class ProxyModel : public QAbstractProxyModel
{
public:
    explicit ProxyModel(QObject *parent = nullptr);

    QModelIndex mapFromSource(const QModelIndex & sourceIndex) const override;
    QModelIndex mapToSource(const QModelIndex & proxyIndex) const override;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    void setSourceModel(QAbstractItemModel *sourceModel) override;

    QVariant data(const QModelIndex &index, int role) const override;

    // QAbstractProxyModel::sibling is broken in Qt 5
    QModelIndex sibling(int row, int column, const QModelIndex &idx) const override;
    // QAbstractProxyModel::supportedDragActions delegation is missing in Qt 5
    Qt::DropActions supportedDragActions() const override;

private:
    void sourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    void sourceRowsRemoved(const QModelIndex &parent, int start, int end);
    void sourceRowsInserted(const QModelIndex &parent, int start, int end);
    void sourceRowsAboutToBeRemoved(const QModelIndex &parent, int start, int end);
    void sourceRowsAboutToBeInserted(const QModelIndex &parent, int start, int end);
};

} // namespace Core::Internal

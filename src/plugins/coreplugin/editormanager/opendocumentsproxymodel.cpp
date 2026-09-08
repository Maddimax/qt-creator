// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "opendocumentsproxymodel.h"

#include "documentmodel.h"
#include "../iversioncontrol.h"
#include "../vcsmanager.h"

#include <utils/fsengine/fileiconprovider.h>
#include <utils/treemodel.h>   // FilePathRole

using namespace Utils;

namespace Core::Internal {

ProxyModel::ProxyModel(QObject *parent) : QAbstractProxyModel(parent)
{
}

QModelIndex ProxyModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    // root
    if (!sourceIndex.isValid())
        return QModelIndex();
    // hide the <no document>
    int row = sourceIndex.row() - 1;
    if (row < 0)
        return QModelIndex();
    return createIndex(row, sourceIndex.column());
}

QModelIndex ProxyModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid())
        return QModelIndex();
    // handle missing <no document>
    return sourceModel()->index(proxyIndex.row() + 1, proxyIndex.column());
}

QModelIndex ProxyModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || row < 0 || row >= sourceModel()->rowCount(mapToSource(parent)) - 1
            || column < 0 || column > 1)
        return QModelIndex();
    return createIndex(row, column);
}

QModelIndex ProxyModel::parent(const QModelIndex &child) const
{
    Q_UNUSED(child)
    return QModelIndex();
}

int ProxyModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return sourceModel()->rowCount(mapToSource(parent)) - 1;
    return 0;
}

int ProxyModel::columnCount(const QModelIndex &parent) const
{
    return sourceModel()->columnCount(mapToSource(parent));
}

void ProxyModel::setSourceModel(QAbstractItemModel *sm)
{
    QAbstractItemModel *previousModel = sourceModel();
    if (previousModel) {
        disconnect(previousModel, &QAbstractItemModel::dataChanged,
                   this, &ProxyModel::sourceDataChanged);
        disconnect(previousModel, &QAbstractItemModel::rowsInserted,
                   this, &ProxyModel::sourceRowsInserted);
        disconnect(previousModel, &QAbstractItemModel::rowsRemoved,
                   this, &ProxyModel::sourceRowsRemoved);
        disconnect(previousModel, &QAbstractItemModel::rowsAboutToBeInserted,
                   this, &ProxyModel::sourceRowsAboutToBeInserted);
        disconnect(previousModel, &QAbstractItemModel::rowsAboutToBeRemoved,
                   this, &ProxyModel::sourceRowsAboutToBeRemoved);
    }
    QAbstractProxyModel::setSourceModel(sm);
    if (sm) {
        connect(sm, &QAbstractItemModel::dataChanged,
                this, &ProxyModel::sourceDataChanged);
        connect(sm, &QAbstractItemModel::rowsInserted,
                this, &ProxyModel::sourceRowsInserted);
        connect(sm, &QAbstractItemModel::rowsRemoved,
                this, &ProxyModel::sourceRowsRemoved);
        connect(sm, &QAbstractItemModel::rowsAboutToBeInserted,
                this, &ProxyModel::sourceRowsAboutToBeInserted);
        connect(sm, &QAbstractItemModel::rowsAboutToBeRemoved,
                this, &ProxyModel::sourceRowsAboutToBeRemoved);
    }
}

QVariant ProxyModel::data(const QModelIndex &index, int role) const
{
    auto filePath = [this, index] {
        return FilePath::fromVariant(QAbstractProxyModel::data(index, FilePathRole));
    };

    if (role == Qt::DecorationRole && index.column() == 0) {
        const QVariant sourceDecoration = QAbstractProxyModel::data(index, role);
        if (sourceDecoration.isValid())
            return sourceDecoration;
        return FileIconProvider::icon(filePath());
    } else if (role == Qt::ForegroundRole) {
        const VcsFileState state = VcsManager::fileState(filePath());
        return VcsManager::fileStateColor(state);
    } else if (role == Qt::ToolTipRole) {
        const FilePath path = filePath();
        const VcsFileState state = VcsManager::fileState(path);
        QString toolTip = path.toUserOutput();
        if (state != VcsFileState::Unknown) {
            const QString stateText = VcsManager::fileStateDescription(state);
            toolTip += "<p>" + stateText;
        }
        return toolTip;
    }

    return QAbstractProxyModel::data(index, role);
}

QModelIndex ProxyModel::sibling(int row, int column, const QModelIndex &idx) const
{
    return QAbstractItemModel::sibling(row, column, idx);
}

Qt::DropActions ProxyModel::supportedDragActions() const
{
    return sourceModel()->supportedDragActions();
}

void ProxyModel::sourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    QModelIndex topLeftIndex = mapFromSource(topLeft);
    if (!topLeftIndex.isValid())
        topLeftIndex = index(0, topLeft.column());
    QModelIndex bottomRightIndex = mapFromSource(bottomRight);
    if (!bottomRightIndex.isValid())
        bottomRightIndex = index(0, bottomRight.column());
    emit dataChanged(topLeftIndex, bottomRightIndex);
}

void ProxyModel::sourceRowsRemoved(const QModelIndex &parent, int start, int end)
{
    Q_UNUSED(parent)
    Q_UNUSED(start)
    Q_UNUSED(end)
    endRemoveRows();
}

void ProxyModel::sourceRowsInserted(const QModelIndex &parent, int start, int end)
{
    Q_UNUSED(parent)
    Q_UNUSED(start)
    Q_UNUSED(end)
    endInsertRows();
}

void ProxyModel::sourceRowsAboutToBeRemoved(const QModelIndex &parent, int start, int end)
{
    int realStart = parent.isValid() || start == 0 ? start : start - 1;
    int realEnd = parent.isValid() || end == 0 ? end : end - 1;
    beginRemoveRows(parent, realStart, realEnd);
}

void ProxyModel::sourceRowsAboutToBeInserted(const QModelIndex &parent, int start, int end)
{
    int realStart = parent.isValid() || start == 0 ? start : start - 1;
    int realEnd = parent.isValid() || end == 0 ? end : end - 1;
    beginInsertRows(parent, realStart, realEnd);
}

} // namespace Core::Internal

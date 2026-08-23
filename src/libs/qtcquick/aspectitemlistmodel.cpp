// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectitemlistmodel.h"

#include "aspectcontainermodel.h"

#include <utils/algorithm.h>
#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/qtcassert.h>

#include <QFuture>

using namespace Utils;

namespace QtcQuick {

AspectItemListModel::AspectItemListModel(AspectList *list, QObject *parent)
    : QAbstractListModel(parent)
    , m_list(list)
{
    QTC_ASSERT(m_list, return);
    reload();
    connect(m_list, &AspectList::volatileItemListChanged, this, &AspectItemListModel::reload);
}

void AspectItemListModel::reload()
{
    beginResetModel();

    // An item that has been removed but not applied yet keeps its row, struck
    // through, the way the widget editor shows it. So the rows are the applied
    // items in their order, marked removed where they are no longer volatile,
    // followed by the ones added since.
    const QList<std::shared_ptr<BaseAspect>> volatileItems = m_list->volatileItems();
    // With auto-apply there is no pending state to show, and the applied list
    // is only brought up to date after volatileItemListChanged() - so reading
    // it here would leave a removed row struck through for good.
    const QList<std::shared_ptr<BaseAspect>> appliedItems
        = m_list->isAutoApply() ? volatileItems : m_list->items();
    const auto isVolatile = [&volatileItems](const std::shared_ptr<BaseAspect> &item) {
        return volatileItems.contains(item);
    };

    m_rows.clear();
    for (const std::shared_ptr<BaseAspect> &item : appliedItems)
        m_rows.append({item.get(), false, !isVolatile(item)});
    for (const std::shared_ptr<BaseAspect> &item : volatileItems) {
        if (!appliedItems.contains(item))
            m_rows.append({item.get(), true, false});
    }

    // Models for items that are gone would dangle; the rest are rebuilt on
    // demand, which costs nothing until a row is shown.
    qDeleteAll(m_itemModels);
    m_itemModels.clear();
    endResetModel();
}

int AspectItemListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant AspectItemListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    BaseAspect *item = row.item;
    switch (role) {
    case AddedRole:
        return row.added;
    case RemovedRole:
        return row.removed;
    case LabelRole: {
        QTC_ASSERT(m_list->listViewDataCallback, return {});
        const QVariant label = m_list->listViewDataCallback(item, Qt::DisplayRole);
        // A label can arrive as a future - the MCP server list names itself
        // asynchronously. Show nothing until it is there, then say so.
        if (label.canConvert<QFuture<QVariant>>()) {
            QFuture<QVariant> pending = label.value<QFuture<QVariant>>();
            if (!pending.isFinished()) {
                auto self = const_cast<AspectItemListModel *>(this);
                pending.then(self, [self, index](const QVariant &) {
                    emit self->dataChanged(index, index, {LabelRole});
                });
                // An empty string rather than an invalid QVariant: the row
                // declares label as a required string, and QML cannot assign
                // undefined to one - the delegate would fail to be created.
                return QString();
            }
            return pending.result();
        }
        return label;
    }
    case ItemModelRole: {
        auto container = qobject_cast<AspectContainer *>(item);
        if (!container)
            return {};
        AspectContainerModel *&model = m_itemModels[item];
        if (!model)
            model = new AspectContainerModel(container, const_cast<AspectItemListModel *>(this));
        return QVariant::fromValue(model);
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> AspectItemListModel::roleNames() const
{
    return {
        {LabelRole, "label"},
        {ItemModelRole, "itemModel"},
        {AddedRole, "added"},
        {RemovedRole, "removed"},
    };
}

int AspectItemListModel::addItem()
{
    QTC_ASSERT(m_list, return -1);
    const std::shared_ptr<BaseAspect> item = m_list->createAndAddItem();
    // The aspect emits volatileItemListChanged(), so the row exists by now.
    return int(Utils::indexOf(m_rows, [&item](const Row &row) {
        return row.item == item.get();
    }));
}

void AspectItemListModel::removeItem(int row)
{
    QTC_ASSERT(m_list, return);
    QTC_ASSERT(row >= 0 && row < m_rows.size(), return);
    // Removing a row that is already struck through would do nothing anyway.
    if (m_rows.at(row).removed)
        return;

    BaseAspect *item = m_rows.at(row).item;
    const QList<std::shared_ptr<BaseAspect>> items = m_list->volatileItems();
    const auto shared = Utils::findOr(items, {}, [item](const std::shared_ptr<BaseAspect> &i) {
        return i.get() == item;
    });
    QTC_ASSERT(shared, return);
    m_list->removeItem(shared);
}

} // namespace QtcQuick

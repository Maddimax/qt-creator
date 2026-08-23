// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectitemlistmodel.h"

#include "aspectcontainermodel.h"

#include <utils/algorithm.h>
#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/qtcassert.h>

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
    m_items = Utils::transform(m_list->volatileItems(),
                               [](const std::shared_ptr<BaseAspect> &item) { return item.get(); });
    // Models for items that are gone would dangle; the rest are rebuilt on
    // demand, which costs nothing until a row is shown.
    qDeleteAll(m_itemModels);
    m_itemModels.clear();
    endResetModel();
}

int AspectItemListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant AspectItemListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};

    BaseAspect *item = m_items.at(index.row());
    switch (role) {
    case LabelRole:
        QTC_ASSERT(m_list->listViewDataCallback, return {});
        return m_list->listViewDataCallback(item, Qt::DisplayRole);
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
    };
}

int AspectItemListModel::addItem()
{
    QTC_ASSERT(m_list, return -1);
    const std::shared_ptr<BaseAspect> item = m_list->createAndAddItem();
    // The aspect emits volatileItemListChanged(), so the row exists by now.
    return int(m_items.indexOf(item.get()));
}

void AspectItemListModel::removeItem(int row)
{
    QTC_ASSERT(m_list, return);
    QTC_ASSERT(row >= 0 && row < m_items.size(), return);

    BaseAspect *item = m_items.at(row);
    const QList<std::shared_ptr<BaseAspect>> items = m_list->volatileItems();
    const auto shared = Utils::findOr(items, {}, [item](const std::shared_ptr<BaseAspect> &i) {
        return i.get() == item;
    });
    QTC_ASSERT(shared, return);
    m_list->removeItem(shared);
}

} // namespace QtcQuick

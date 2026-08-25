// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectitemlistmodel.h"

#include "aspectcontainermodel.h"
#include "qtciconprovider.h"

#include <utils/algorithm.h>
#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/qtcassert.h>

#include <QFuture>
#include <QIcon>

using namespace Utils;

namespace QtcQuick {

// A list that answers asynchronously - the MCP server list names itself once
// it has connected - hands back a future for every role, not just the one it
// meant. Only the label knows what to do with that; a colour or an icon that
// is not there yet is simply not there.
static QVariant withoutFuture(const QVariant &value)
{
    return value.canConvert<QFuture<QVariant>>() ? QVariant() : value;
}

AspectItemListModel::AspectItemListModel(AspectList *list, QObject *parent)
    : QAbstractListModel(parent)
    , m_list(list)
{
    QTC_ASSERT(m_list, return);
    reload();
    connect(m_list, &AspectList::volatileItemListChanged, this, &AspectItemListModel::reload);
    // Apply says changed(), not volatileItemListChanged(), and it is what makes
    // a removed row finally go away.
    connect(m_list, &AspectList::changed, this, &AspectItemListModel::reload);
    // A row says what an item is called and what colour it means, both of
    // which the details pane edits. Nothing else tells the row it changed, so
    // renaming an item left the list showing the old name.
    connect(m_list, &AspectList::volatileValueChanged, this, [this] {
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(int(m_rows.size()) - 1));
    });
}

void AspectItemListModel::reload()
{
    beginResetModel();

    // The rows are the volatile items in their order - which is the user's on a
    // list that may be reordered - with the ones added since Apply marked. An
    // item that has been removed but not applied yet keeps its row, struck
    // through and where it was, the way the widget editor shows it.
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
    for (const std::shared_ptr<BaseAspect> &item : volatileItems)
        m_rows.append({item, !appliedItems.contains(item), false});
    for (int row = 0; row < appliedItems.size(); ++row) {
        const std::shared_ptr<BaseAspect> &item = appliedItems.at(row);
        if (!isVolatile(item))
            m_rows.insert(std::min(row, int(m_rows.size())), {item, false, true});
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
    BaseAspect *item = row.item.get();
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
    case DecorationRole: {
        QTC_ASSERT(m_list->listViewDataCallback, return {});
        const QVariant decoration = m_list->listViewDataCallback(item, Qt::DecorationRole);
        if (decoration.canConvert<QIcon>())
            return iconUrl(decoration.value<QIcon>());
        return withoutFuture(decoration);
    }
    case ForegroundRole: {
        QTC_ASSERT(m_list->listViewDataCallback, return {});
        return withoutFuture(m_list->listViewDataCallback(item, Qt::ForegroundRole));
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
        {DecorationRole, "decoration"},
        {ForegroundRole, "itemForeground"},
    };
}

QStringList AspectItemListModel::extraButtons() const
{
    QTC_ASSERT(m_list, return {});
    return m_list->extraButtonTexts();
}

void AspectItemListModel::triggerExtraButton(int index)
{
    QTC_ASSERT(m_list, return);
    m_list->triggerExtraButton(index);
}

int AspectItemListModel::addItem()
{
    QTC_ASSERT(m_list, return -1);
    const std::shared_ptr<BaseAspect> item = m_list->createAndAddItem();
    // The aspect emits volatileItemListChanged(), so the row exists by now.
    return int(Utils::indexOf(m_rows, [&item](const Row &row) {
        return row.item == item;
    }));
}

void AspectItemListModel::removeItem(int row)
{
    QTC_ASSERT(m_list, return);
    QTC_ASSERT(row >= 0 && row < m_rows.size(), return);
    // Removing a row that is already struck through would do nothing anyway.
    if (m_rows.at(row).removed)
        return;

    m_list->removeItem(m_rows.at(row).item);
}

} // namespace QtcQuick

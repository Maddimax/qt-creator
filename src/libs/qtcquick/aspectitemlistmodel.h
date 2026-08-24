// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QAbstractListModel>
#include <QHash>
#include <QQmlEngine>

#include <memory>

namespace Utils {
class AspectList;
class BaseAspect;
} // namespace Utils

namespace QtcQuick {

class AspectContainerModel;

// The items of a Utils::AspectList: one row each, with a model for the item's
// own aspects so a delegate can show the selected one's settings. Adding and
// removing go through the aspect, which is what records them for Apply.
class QTCQUICK_EXPORT AspectItemListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created from C++ by createAspectForm()")

    // Buttons the list offers besides Add and Remove, in order. Fixed once the
    // aspect is set up, which is before any of this is built.
    Q_PROPERTY(QStringList extraButtons READ extraButtons CONSTANT)

public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        ItemModelRole,
        // An item added or removed since the last Apply. The widget editor
        // shows the first in bold and the second struck through.
        AddedRole,
        RemovedRole,
        // The row's icon, as a URL an Image can load - the callback answers a
        // QIcon, which QML cannot carry. See QtcQuick::iconUrl().
        DecorationRole,
        // What colour to write the row's label in, where the list means
        // something by it. Unset leaves it the form's.
        ForegroundRole,
    };

    explicit AspectItemListModel(Utils::AspectList *list, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Returns the row of the new item, so a view can select it.
    Q_INVOKABLE int addItem();
    Q_INVOKABLE void removeItem(int row);

    QStringList extraButtons() const;
    Q_INVOKABLE void triggerExtraButton(int index);

private:
    void reload();

    struct Row
    {
        // Shared rather than raw: applying a removal drops the aspect's last
        // reference inside AspectList::apply(), before it says anything, so a
        // raw pointer here would dangle for the rest of that call.
        std::shared_ptr<Utils::BaseAspect> item;
        bool added = false;
        bool removed = false;
    };

    Utils::AspectList *m_list = nullptr;
    QList<Row> m_rows;
    mutable QHash<Utils::BaseAspect *, AspectContainerModel *> m_itemModels;
};

} // namespace QtcQuick

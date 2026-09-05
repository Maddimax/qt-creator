// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "actionmodel.h"

#include "qtciconprovider.h"

#include <QAction>
#include <QMenu>

namespace QtcQuick {

ActionModel::ActionModel(QObject *parent)
    : QAbstractListModel(parent)
{}

void ActionModel::setActions(const QList<QAction *> &actions)
{
    beginResetModel();
    for (const QPointer<QAction> &action : std::as_const(m_actions)) {
        if (action)
            disconnect(action, nullptr, this, nullptr);
    }

    m_actions.clear();
    for (QAction * const action : actions) {
        m_actions.append(action);
        if (!action)
            continue;
        // An action changes under an open menu: a command's text says what it
        // will do next, and what is enabled depends on where the caret is.
        const int row = int(m_actions.size()) - 1;
        connect(action, &QAction::changed, this, [this, row] {
            const QModelIndex at = index(row, 0);
            emit dataChanged(at, at);
        });
    }
    endResetModel();
}

void ActionModel::setProvider(const Provider &provider)
{
    m_provider = provider;
    refresh();
}

void ActionModel::refresh()
{
    if (m_provider)
        setActions(m_provider());
}

int ActionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_actions.size());
}

// The menu \a action carries, as a model. Asked again every time it is shown,
// because a menu assembled by a plugin gains entries after the action exists.
ActionModel *ActionModel::submenuFor(QAction *action) const
{
    QMenu * const menu = action ? action->menu() : nullptr;
    if (!menu)
        return nullptr;
    ActionModel *&model = m_submenus[action];
    if (!model) {
        model = new ActionModel(const_cast<ActionModel *>(this));
        model->setProvider([menu] { return menu->actions(); });
    }
    model->refresh();
    return model;
}

QVariant ActionModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= int(m_actions.size()))
        return {};

    const QAction * const action = m_actions.at(index.row());
    // A null action, or one that has gone away, is the separator a QMenu would
    // have drawn there.
    if (!action || action->isSeparator())
        return role == SeparatorRole ? QVariant(true) : QVariant();

    switch (role) {
    case TextRole:
        return action->text();
    case ShortcutRole:
        return action->shortcut().toString(QKeySequence::NativeText);
    case EnabledRole:
        return action->isEnabled();
    case VisibleRole:
        return action->isVisible();
    case CheckableRole:
        return action->isCheckable();
    case CheckedRole:
        return action->isChecked();
    case SeparatorRole:
        return false;
    case IconRole:
        return iconUrl(action->icon());
    case ToolTipRole:
        return action->toolTip();
    case MenuRole:
        return QVariant::fromValue(submenuFor(const_cast<QAction *>(action)));
    default:
        return {};
    }
}

QHash<int, QByteArray> ActionModel::roleNames() const
{
    // Every name prefixed, because a MenuItem already has text, enabled,
    // checkable and checked of its own, and a delegate cannot declare a
    // required property that shadows one of those.
    return {{TextRole, "actionText"},
            {ShortcutRole, "actionShortcut"},
            {EnabledRole, "actionEnabled"},
            {VisibleRole, "actionVisible"},
            {CheckableRole, "actionCheckable"},
            {CheckedRole, "actionChecked"},
            {SeparatorRole, "actionSeparator"},
            {IconRole, "actionIcon"},
            {ToolTipRole, "actionToolTip"},
            {MenuRole, "actionMenu"}};
}

void ActionModel::trigger(int row)
{
    if (row < 0 || row >= int(m_actions.size()))
        return;
    // No isEnabled() check: QAction::trigger() on a disabled action already
    // does nothing, which matters because a Quick MenuItem can be told to look
    // disabled and still be told to fire.
    QAction * const action = m_actions.at(row);
    if (action && !action->isSeparator())
        action->trigger();
}

} // namespace QtcQuick

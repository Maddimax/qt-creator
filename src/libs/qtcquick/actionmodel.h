// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QQmlEngine>

#include <functional>

QT_BEGIN_NAMESPACE
class QAction;
QT_END_NAMESPACE

namespace QtcQuick {

// A list of QActions as a model a Qt Quick Menu can instantiate. Qt Creator's
// menus are assembled by the ActionManager out of QActions from every plugin
// that wants a say, and that is not something worth reinventing per menu - so
// a ported menu takes the actions it would have shown and lists them here.
//
// Enabled, checked, visible and text all change while a menu is open (an
// action's text is rewritten to say what it will do next), so each is a role
// and each action's changed() signal is followed.
class QTCQUICK_EXPORT ActionModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        ShortcutRole,
        EnabledRole,
        VisibleRole,
        CheckableRole,
        CheckedRole,
        // The action's own font. A tool bar takes the weight and the slant
        // from it and leaves the size and family to its own type scale.
        FontRole,
        SeparatorRole,
        IconRole,
        // What the action says about itself when pointed at, shortcut and
        // all. A menu shows the shortcut in its own column; a toolbar button
        // has only this.
        ToolTipRole,
        // The entries of this action's own menu, as a model of their own, or
        // null where it has none. An action carrying a menu is one that opens
        // it rather than one that triggers - which is what a QToolButton set
        // to InstantPopup does in the widget editor.
        MenuRole,
    };

    explicit ActionModel(QObject *parent = nullptr);

    // Nulls are allowed and become separators, which is how a QMenu carries
    // them too - so a caller can hand over menu->actions() unfiltered.
    void setActions(const QList<QAction *> &actions);

    // Where the actions come from, asked again every time refresh() is called.
    // A menu assembled by the ActionManager gains and loses entries as plugins
    // register them, so a list taken once is right only until it is not.
    using Provider = std::function<QList<QAction *>()>;
    void setProvider(const Provider &provider);
    Q_INVOKABLE void refresh();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Triggers the action at \a row, or nothing for a separator. A disabled
    // one is safe to pass: QAction refuses it, which matters because a Quick
    // MenuItem can be told to look disabled and still be told to fire.
    Q_INVOKABLE void trigger(int row);

private:
    ActionModel *submenuFor(QAction *action) const;

private:
    QList<QPointer<QAction>> m_actions;
    Provider m_provider;
    // One per action that has a menu, made when first asked for and owned by
    // this: a delegate binds to it, so it has to outlive the binding.
    mutable QHash<QAction *, ActionModel *> m_submenus;
};

} // namespace QtcQuick

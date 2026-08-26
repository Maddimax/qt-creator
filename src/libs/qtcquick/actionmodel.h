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
        SeparatorRole,
        IconRole,
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
    QList<QPointer<QAction>> m_actions;
    Provider m_provider;
};

} // namespace QtcQuick

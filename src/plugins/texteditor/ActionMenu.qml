// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// A menu over an ActionModel. Qt Creator's menus are QActions assembled by the
// ActionManager out of every plugin that wants a say, so this lists what that
// produced rather than naming any of it - see QtcQuick::ActionModel.
//
// Its own component because the editor has two such menus, the one the text
// offers and the one the gutter does, and they differ only in which model
// they read.
Menu {
    id: root

    property ActionModel actions: null

    Repeater {
        model: root.actions

        delegate: MenuItem {
            id: entry

            required property int index
            required property string actionText
            required property string actionShortcut
            required property bool actionEnabled
            required property bool actionVisible
            required property bool actionCheckable
            required property bool actionChecked
            required property bool actionSeparator

            text: entry.actionSeparator ? "" : entry.actionText
            enabled: !entry.actionSeparator && entry.actionEnabled
            visible: entry.actionVisible
            checkable: entry.actionCheckable
            checked: entry.actionChecked

            // A separator is an entry with nothing in it and a rule drawn
            // through it, rather than a MenuSeparator: a Repeater's
            // delegate is one type, and a Menu treats its MenuItems
            // specially enough that swapping the type is not worth it.
            //
            // A Binding rather than a conditional: the other arm would be
            // "whatever the style says", and there is no way to write that
            // - assigning undefined to a double is an error, not a reset.
            Binding on implicitHeight {
                when: entry.actionSeparator
                value: Spacing.GapVM
                restoreMode: Binding.RestoreBindingOrValue
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                visible: entry.actionSeparator
                color: Tokens.strokeSubtle
            }

            onTriggered: root.actions.trigger(entry.index)
        }
    }
}

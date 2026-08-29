// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// The right-click menu on a control, for a state that is about the setting
// rather than about its value - whether a kit aspect may be changed per run
// configuration. Attached by a delegate to whatever it draws; does nothing
// where the aspect offers no such state.
//
// A MouseArea rather than a TapHandler: the control underneath must keep
// working, and only the right button is taken.
MouseArea {
    id: root

    objectName: "aspectContextMenu"

    required property Aspect aspect
    required property var pres

    readonly property string actionText: root.pres?.contextActionText ?? ""

    anchors.fill: parent
    acceptedButtons: Qt.RightButton
    enabled: actionText !== ""
    onClicked: menu.popup()

    Menu {
        id: menu

        objectName: "aspectContextMenuPopup"

        MenuItem {
            objectName: "aspectContextAction"
            text: root.actionText
            checkable: true
            checked: root.pres?.contextActionChecked ?? false
            enabled: root.pres?.contextActionEnabled ?? true
            onTriggered: root.aspect?.triggerContextAction(checked)
        }
    }
}

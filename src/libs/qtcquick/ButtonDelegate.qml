// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// One button and nothing else: a page action that has no value. The counterpart
// of the PushButton a Layouting closure used to build.
RowLayout {
    id: root

    required property Aspect aspect
    // The descriptor, read from the aspect rather than taken as model roles, so
    // that a hand-written page can use this delegate with nothing but the
    // aspect. See AspectModels::presentation().
    // Not readonly: an aspect can change what it wants drawn - the label on
    // Copilot's sign-in button is its state - and says so with
    // controlConfigurationChanged().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    // What the button offers, empty where it just acts. A popup is not in the
    // item tree, so this is the only way to reach it from outside.
    readonly property alias menu: menu

    Connections {
        target: root.aspect
        function onControlConfigurationChanged() {
            root.pres = AspectModels.presentation(root.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    // The aspect is being drawn, so let it find out what its label should say.
    Component.onCompleted: root.aspect?.requestDisplayText()

    Button {
        id: button

        // A button that offers rather than does has one entry per choice.
        readonly property var options: root.pres.options ?? []

        text: root.pres.actionText ?? ""
        enabled: (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)
        ToolTip.text: root.toolTip
        ToolTip.visible: hovered && root.toolTip !== ""
        onClicked: {
            if (button.options.length === 0)
                root.aspect?.triggerAction()
            else
                menu.popup(button, 0, button.height)
        }
    }

    Menu {
        id: menu

        Instantiator {
            model: root.pres.options ?? []
            onObjectAdded: (index, object) => menu.insertItem(index, object as MenuItem)
            onObjectRemoved: (index, object) => menu.removeItem(object as MenuItem)

            delegate: MenuItem {
                required property int index
                required property string modelData
                text: modelData
                enabled: root.pres.optionsEnabled[index] ?? true
                onTriggered: root.aspect?.triggerChoice(root.pres.optionIds[index])
            }
        }
    }

    Item { Layout.fillWidth: true }
}

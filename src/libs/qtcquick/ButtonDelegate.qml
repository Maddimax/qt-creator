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

    required property ActionAspect aspect
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
    // The button that opens that menu beside one which acts. Its glyph is an
    // image, so there is no text to find it by.
    readonly property alias arrowButton: arrow

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

    Button {
        id: button

        // A button that offers rather than does has one entry per choice.
        readonly property var options: root.pres.options ?? []

        text: root.pres.actionText ?? ""
        icon.source: root.pres.actionIcon ?? ""
        enabled: (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)
        ToolTip.text: root.toolTip
        ToolTip.visible: hovered && root.toolTip !== ""
        onClicked: {
            // A button that also acts keeps its click; one that only offers
            // opens the menu instead.
            if (button.options.length === 0 || (root.pres.actionIsDefault ?? false))
                root.aspect?.triggerAction()
            else
                menu.popup(button, 0, button.height)
        }
    }

    // The arrow beside a button that does something of its own: what it offers
    // is a shortcut, not the only way in.
    Button {
        id: arrow

        visible: button.options.length > 0 && (root.pres.actionIsDefault ?? false)
        enabled: button.enabled
        Layout.preferredWidth: implicitHeight
        onClicked: menu.popup(arrow, 0, arrow.height)

        // The glyph is an image rather than a character: U+25BE is not in
        // every UI font, and the button came out blank where it is not.
        contentItem: Image {
            source: "image://qtcreator/utils/images/arrowdown.png?color=Token_Text_Muted"
            fillMode: Image.Pad
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            opacity: arrow.enabled ? 1.0 : Metrics.disabledIconOpacity
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

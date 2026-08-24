// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

RowLayout {
    id: root

    required property Aspect aspect
    // The descriptor. This delegate had none, so nothing it says was read -
    // the reset button it asks for was simply missing.
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: root.aspect
        function onControlConfigurationChanged() {
            root.pres = AspectModels.presentation(root.aspect)
        }
    }

    readonly property color currentColor: root.aspect?.value ?? Tokens.backgroundMuted
    readonly property bool controlsEnabled:
        (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // A palette is a row of swatches with no labels at all; an aspect with
        // no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
    }

    Rectangle {
        implicitWidth: Metrics.colorSwatchSize
        implicitHeight: Metrics.colorSwatchSize
        radius: Spacing.RadiusS
        color: root.currentColor
        border.width: 1
        border.color: Tokens.strokeMuted

        ToolTip.text: root.toolTip
        ToolTip.visible: swatchHover.hovered && root.toolTip !== ""

        HoverHandler { id: swatchHover }
    }

    Label {
        text: qsTr("R")
    }

    SpinBox {
        id: redBox
        from: 0
        to: 255
        editable: true
        enabled: root.controlsEnabled
        value: Math.round(root.currentColor.r * 255)

        onValueModified: if (root.aspect)
            root.aspect.value = Qt.rgba(value / 255, greenBox.value / 255,
                                        blueBox.value / 255, root.currentColor.a)
    }

    Label {
        text: qsTr("G")
    }

    SpinBox {
        id: greenBox
        from: 0
        to: 255
        editable: true
        enabled: root.controlsEnabled
        value: Math.round(root.currentColor.g * 255)

        onValueModified: if (root.aspect)
            root.aspect.value = Qt.rgba(redBox.value / 255, value / 255,
                                        blueBox.value / 255, root.currentColor.a)
    }

    Label {
        text: qsTr("B")
    }

    SpinBox {
        id: blueBox
        from: 0
        to: 255
        editable: true
        enabled: root.controlsEnabled
        value: Math.round(root.currentColor.b * 255)

        onValueModified: if (root.aspect)
            root.aspect.value = Qt.rgba(redBox.value / 255, greenBox.value / 255,
                                        value / 255, root.currentColor.a)
    }

    // A colour that can be put back to its default offers a button for it, the
    // way the widget picker does. This is the "unset" beside a syntax format's
    // colour, so it is not decoration.
    Button {
        objectName: "colorResetButton"
        text: qsTr("Reset")
        visible: root.pres.withResetButton ?? false
        enabled: root.controlsEnabled
        ToolTip.text: qsTr("Reset to default.", "Color")
        ToolTip.visible: hovered
        onClicked: root.aspect?.resetToDefault()
    }

    Item { Layout.fillWidth: true }
}

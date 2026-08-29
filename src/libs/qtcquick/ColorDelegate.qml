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

    // A palette is a grid of swatches, and the numbers do not fit beside
    // sixteen of them - eight to a row is 3800 pixels of spin box. Compact
    // moves them behind the swatch instead of dropping them, so a palette
    // colour stays as editable as any other.
    property bool compact: false

    readonly property color currentColor: root.aspect?.value ?? Tokens.backgroundMuted
    readonly property bool controlsEnabled:
        (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)
    // How see-through the colour is. Editable unless the aspect says the
    // colour is opaque or nothing - which two of them do, and which is why
    // the widget picker asks before offering an alpha channel.
    readonly property bool alphaAllowed: root.pres.alphaAllowed ?? true
    readonly property real alpha: root.currentColor.a

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
        id: swatch

        objectName: "colorSwatch"
        implicitWidth: Metrics.colorSwatchSize
        implicitHeight: Metrics.colorSwatchSize
        radius: Spacing.RadiusS
        color: root.currentColor
        border.width: 1
        border.color: swatchHover.hovered && root.compact ? Tokens.strokeStrong
                                                          : Tokens.strokeMuted

        ToolTip.text: root.toolTip
        ToolTip.visible: swatchHover.hovered && root.toolTip !== ""

        HoverHandler { id: swatchHover }

        TapHandler {
            enabled: root.compact && root.controlsEnabled
            onTapped: picker.open()
        }

        Popup {
            id: picker

            objectName: "colorPicker"
            y: swatch.height + Spacing.GapVXs
            padding: Spacing.PaddingHS

            contentItem: ColorNumbers {}
        }
    }

    ColorNumbers {
        visible: !root.compact
    }

    // Declared once and used twice: inline beside the swatch, and inside the
    // popup a compact swatch opens.
    component ColorNumbers: RowLayout {
        spacing: Spacing.GapHM

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
                                            blueBox.value / 255, root.alpha)
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
                                            blueBox.value / 255, root.alpha)
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
                                            value / 255, root.alpha)
        }

        Label {
            text: qsTr("A")
            visible: root.alphaAllowed
        }

        SpinBox {
            id: alphaBox
            from: 0
            to: 255
            editable: true
            visible: root.alphaAllowed
            enabled: root.controlsEnabled
            value: Math.round(root.currentColor.a * 255)

            onValueModified: if (root.aspect)
                root.aspect.value = Qt.rgba(redBox.value / 255, greenBox.value / 255,
                                            blueBox.value / 255, value / 255)
        }
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

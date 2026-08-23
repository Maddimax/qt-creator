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
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    readonly property color currentColor: root.aspect?.value ?? Tokens.backgroundMuted
    readonly property bool controlsEnabled:
        (root.aspect?.enabled ?? false) && !(root.aspect?.readOnly ?? false)

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
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

    Item { Layout.fillWidth: true }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCreator.Ui

// A colour theme: which one is being edited, and the colours in it. The
// swatches overlap in a cascade, each one showing what a state of that colour
// looks like; clicking one picks a new colour for it.
AspectPage {
    id: root

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHXs
        Layout.fillWidth: true

        SelectionDelegate {
            aspect: root.aspects.Theme
            Layout.fillWidth: true
        }

        ButtonDelegate { aspect: root.aspects.AddTheme }
        ButtonDelegate { aspect: root.aspects.RemoveTheme }
    }

    Item {
        id: cascade

        // The colours the aspect holds, as names. Not readonly: the aspect
        // replaces the whole list when another theme is shown.
        property var colours: root.aspects.Colors.value ?? []
        readonly property bool editable: root.aspects.Colors.enabled ?? false

        // The square each swatch is, and how far the next one is offset from
        // it - the same two the widget's updateItemRects() worked out.
        readonly property real square: Math.min(width / 2, height / 2)
        readonly property real step: cascade.colours.length > 0
                                     ? cascade.square / cascade.colours.length : 0

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Metrics.formListHeight

        Repeater {
            model: cascade.colours

            delegate: Rectangle {
                id: swatch

                required property int index
                required property string modelData

                objectName: "themeColorSwatch"
                x: cascade.step * (swatch.index + 1)
                y: cascade.step * (swatch.index + 1)
                width: cascade.square
                height: cascade.square
                radius: width * 0.1
                border.color: Tokens.strokeMuted
                // Thicker under the pointer, which is how the widget said
                // which one a click would take.
                border.width: hover.hovered && cascade.editable ? 3 : 1
                opacity: cascade.editable ? 1 : 0.5

                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.lighter(swatch.modelData, 1.15) }
                    GradientStop { position: 1; color: swatch.modelData }
                }

                HoverHandler { id: hover; enabled: cascade.editable }

                TapHandler {
                    enabled: cascade.editable
                    onTapped: {
                        picker.forSwatch = swatch.index
                        picker.selectedColor = swatch.modelData
                        picker.open()
                    }
                }
            }
        }
    }

    ColorDialog {
        id: picker

        objectName: "themeColorPicker"

        // Which swatch is being picked for; -1 between openings.
        property int forSwatch: -1

        title: qsTr("Pick Color")
        onAccepted: {
            if (picker.forSwatch >= 0)
                root.aspects.Colors.setColorAt(picker.forSwatch, picker.selectedColor)
            picker.forSwatch = -1
        }
        onRejected: picker.forSwatch = -1
    }
}

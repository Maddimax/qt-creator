// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

Rectangle {
    id: root

    // Set from C++ so the swatch and font grids need no hand-written lists.
    required property var tokenNames
    required property var fontNames
    required property var spacingNames
    required property var iconMasks

    color: Tokens.backgroundDefault

    component SectionLabel: Label {
        font: Fonts.h6Capital
        color: Tokens.textMuted
        Layout.topMargin: Spacing.GapVL
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: root.width
            spacing: Spacing.GapVS

            ColumnLayout {
                Layout.margins: Spacing.PaddingHL
                spacing: Spacing.GapVS

                SectionLabel { text: qsTr("Colour tokens") }

                GridLayout {
                    columns: 4
                    columnSpacing: Spacing.GapHM
                    rowSpacing: Spacing.GapVXs

                    Repeater {
                        model: root.tokenNames
                        delegate: RowLayout {
                            required property string modelData
                            spacing: Spacing.GapHXs

                            Rectangle {
                                width: Spacing.PrimitiveXxl
                                height: Spacing.PrimitiveXl
                                radius: Spacing.RadiusS
                                color: Tokens[parent.modelData]
                                border.width: 1
                                border.color: Tokens.strokeSubtle
                            }
                            Label {
                                text: parent.modelData
                                font: Fonts.caption
                                color: Tokens.textMuted
                            }
                        }
                    }
                }

                SectionLabel { text: qsTr("Type scale") }

                Repeater {
                    model: root.fontNames
                    delegate: RowLayout {
                        required property string modelData
                        spacing: Spacing.GapHM

                        Label {
                            text: parent.modelData
                            font: Fonts.caption
                            color: Tokens.textSubtle
                            Layout.preferredWidth: 140
                        }
                        Rectangle {
                            // The line-height box, so leading is visible.
                            color: Tokens.foregroundSubtle
                            Layout.preferredWidth: label.implicitWidth
                            Layout.preferredHeight: Fonts[parent.modelData + "LineHeight"]

                            Label {
                                id: label
                                anchors.verticalCenter: parent.verticalCenter
                                text: "AaBbCcXxYyZz123"
                                font: Fonts[parent.parent.modelData]
                                color: Tokens.textDefault
                            }
                        }
                    }
                }

                SectionLabel { text: qsTr("Spacing scale") }

                GridLayout {
                    columns: 6
                    columnSpacing: Spacing.GapHM
                    rowSpacing: Spacing.GapVXs

                    Repeater {
                        model: root.spacingNames
                        delegate: RowLayout {
                            required property var modelData
                            spacing: Spacing.GapHXs

                            Rectangle {
                                width: parent.modelData.value
                                height: Spacing.PrimitiveM
                                color: Tokens.accentDefault
                            }
                            Label {
                                text: parent.modelData.name + " (" + parent.modelData.value + ")"
                                font: Fonts.caption
                                color: Tokens.textMuted
                            }
                        }
                    }
                }

                SectionLabel { text: qsTr("Icons") }

                RowLayout {
                    spacing: Spacing.GapHM
                    Repeater {
                        model: root.iconMasks
                        delegate: Image {
                            required property string modelData
                            source: "image://qtcreator/" + modelData + "?color=Token_Text_Muted"
                            sourceSize: Qt.size(16, 16)
                        }
                    }
                }

                SectionLabel { text: qsTr("Controls") }

                GridLayout {
                    columns: 6
                    columnSpacing: Spacing.GapHM
                    rowSpacing: Spacing.GapVS

                    Label { text: qsTr("Button"); font: Fonts.caption }
                    Button { text: qsTr("Primary"); highlighted: true }
                    Button { text: qsTr("Secondary") }
                    Button { text: qsTr("Flat"); flat: true }
                    Button { text: qsTr("Disabled"); enabled: false }
                    Button { text: qsTr("Focus"); focus: true }

                    Label { text: qsTr("CheckBox"); font: Fonts.caption }
                    CheckBox { text: qsTr("Off") }
                    CheckBox { text: qsTr("On"); checked: true }
                    CheckBox { text: qsTr("Partial"); tristate: true
                               checkState: Qt.PartiallyChecked }
                    CheckBox { text: qsTr("Disabled"); enabled: false; checked: true }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("RadioButton"); font: Fonts.caption }
                    RadioButton { text: qsTr("Off") }
                    RadioButton { text: qsTr("On"); checked: true }
                    RadioButton { text: qsTr("Disabled"); enabled: false; checked: true }
                    Item { Layout.fillWidth: true }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("Switch"); font: Fonts.caption }
                    Switch { text: qsTr("Off") }
                    Switch { text: qsTr("On"); checked: true }
                    Switch { text: qsTr("Disabled"); enabled: false; checked: true }
                    Item { Layout.fillWidth: true }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("TextField"); font: Fonts.caption }
                    TextField { placeholderText: qsTr("Placeholder") }
                    TextField { text: qsTr("Some text") }
                    TextField { text: qsTr("Disabled"); enabled: false }
                    TextField { text: qsTr("Read only"); readOnly: true }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("ComboBox"); font: Fonts.caption }
                    ComboBox { model: ["One", "Two", "Three"] }
                    ComboBox { model: ["Disabled"]; enabled: false }
                    SpinBox { value: 42; to: 100 }
                    SpinBox { value: 7; enabled: false }
                    Item { Layout.fillWidth: true }
                }

                SectionLabel { text: qsTr("Containers") }

                RowLayout {
                    spacing: Spacing.GapHM

                    GroupBox {
                        title: qsTr("Group box")
                        ColumnLayout {
                            CheckBox { text: qsTr("Nested check") }
                            Label { text: qsTr("Nested label") }
                        }
                    }

                    ItemDelegate { text: qsTr("Item delegate") }
                    ItemDelegate { text: qsTr("Highlighted"); highlighted: true }

                    Button {
                        text: qsTr("Hover for tooltip")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("A tool tip")
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}

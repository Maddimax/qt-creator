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

                SectionLabel { text: qsTr("Qt Creator components") }

                // Hover and pressed are pointer-driven states with no QML
                // property to force, same as the Controls grid above: move
                // and click the mouse over an instance to see them. No
                // "Focus" cells here - only one item per FocusScope chain
                // can hold real active focus at load (see the pre-existing
                // Button "Focus" cell above), and QtcButton/QtcSwitch's own
                // paintEvent never reacts to hasFocus() anyway. Tab or click
                // into any QtcLineEdit/QtcSearchBox to see its border react.

                GridLayout {
                    columns: 6
                    columnSpacing: Spacing.GapHM
                    rowSpacing: Spacing.GapVS

                    Label { text: qsTr("QtcButton Primary"); font: Fonts.caption }
                    QtcButton { text: qsTr("Large"); role: QtcButton.Role.LargePrimary }
                    QtcButton { text: qsTr("Medium"); role: QtcButton.Role.MediumPrimary }
                    QtcButton { text: qsTr("Small"); role: QtcButton.Role.SmallPrimary }
                    QtcButton { text: qsTr("Disabled"); role: QtcButton.Role.MediumPrimary
                                enabled: false }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("QtcButton Secondary"); font: Fonts.caption }
                    QtcButton { text: qsTr("Large"); role: QtcButton.Role.LargeSecondary }
                    QtcButton { text: qsTr("Medium"); role: QtcButton.Role.MediumSecondary }
                    QtcButton { text: qsTr("Small"); role: QtcButton.Role.SmallSecondary }
                    QtcButton { text: qsTr("Disabled"); role: QtcButton.Role.MediumSecondary
                                enabled: false }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("QtcButton Tertiary"); font: Fonts.caption }
                    QtcButton { text: qsTr("Large"); role: QtcButton.Role.LargeTertiary }
                    QtcButton { text: qsTr("Medium"); role: QtcButton.Role.MediumTertiary }
                    QtcButton { text: qsTr("Small"); role: QtcButton.Role.SmallTertiary }
                    QtcButton { text: qsTr("Disabled"); role: QtcButton.Role.MediumTertiary
                                enabled: false }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("QtcButton Ghost"); font: Fonts.caption }
                    QtcButton { text: qsTr("Large"); role: QtcButton.Role.LargeGhost }
                    QtcButton { text: qsTr("Medium"); role: QtcButton.Role.MediumGhost }
                    QtcButton { text: qsTr("Small"); role: QtcButton.Role.SmallGhost }
                    QtcButton { text: qsTr("Disabled"); role: QtcButton.Role.MediumGhost
                                enabled: false }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("QtcButton List/Link/Tag"); font: Fonts.caption }
                    QtcButton { text: qsTr("List"); role: QtcButton.Role.SmallList }
                    QtcButton { text: qsTr("List checked"); role: QtcButton.Role.SmallList
                                checked: true }
                    QtcButton { text: qsTr("Link"); role: QtcButton.Role.SmallLink }
                    QtcButton { text: qsTr("Tag"); role: QtcButton.Role.Tag }
                    QtcButton { text: qsTr("Tag disabled"); role: QtcButton.Role.Tag
                                enabled: false }

                    Label { text: qsTr("QtcLabel"); font: Fonts.caption }
                    QtcLabel { text: qsTr("Primary label"); role: QtcLabel.Role.Primary }
                    QtcLabel { text: qsTr("SECONDARY LABEL"); role: QtcLabel.Role.Secondary }
                    QtcLabel { text: qsTr("Disabled"); role: QtcLabel.Role.Primary; enabled: false }
                    Item { Layout.fillWidth: true }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("QtcSwitch"); font: Fonts.caption }
                    QtcSwitch { text: qsTr("Off") }
                    QtcSwitch { text: qsTr("On"); checked: true }
                    QtcSwitch { text: qsTr("Disabled"); enabled: false; checked: true }
                    Item { Layout.fillWidth: true }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("QtcLineEdit"); font: Fonts.caption }
                    QtcLineEdit { placeholderText: qsTr("Placeholder") }
                    QtcLineEdit { text: qsTr("Some text") }
                    QtcLineEdit { text: qsTr("Read only"); readOnly: true }
                    QtcLineEdit { text: qsTr("Disabled"); enabled: false }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("QtcSearchBox"); font: Fonts.caption }
                    QtcSearchBox { placeholderText: qsTr("Search") }
                    QtcSearchBox { text: qsTr("query text") }
                    QtcSearchBox { text: qsTr("Disabled"); enabled: false }
                    Item { Layout.fillWidth: true }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("QtcProgressBar"); font: Fonts.caption }
                    QtcProgressBar { value: 70 }
                    QtcProgressBar { value: 25 }
                    QtcProgressBar { value: 0 }
                    QtcProgressBar { value: 70; enabled: false }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("QtcBadge"); font: Fonts.caption }
                    QtcBadge { text: qsTr("3"); role: QtcBadge.Role.NumberPrimary
                               status: QtcBadge.Status.Ok }
                    QtcBadge { text: qsTr("12"); role: QtcBadge.Role.NumberSecondary
                               status: QtcBadge.Status.Information }
                    QtcBadge { text: qsTr("!"); role: QtcBadge.Role.NumberPrimary
                               status: QtcBadge.Status.Warning }
                    QtcBadge { text: qsTr("9+"); role: QtcBadge.Role.NumberPrimary
                               status: QtcBadge.Status.Error }
                    QtcBadge { text: qsTr("3"); role: QtcBadge.Role.NumberPrimary; enabled: false }

                    Label { text: qsTr("QtcPageIndicator"); font: Fonts.caption }
                    QtcPageIndicator { pagesCount: 5; currentPage: 0 }
                    QtcPageIndicator { pagesCount: 5; currentPage: 2 }
                    QtcPageIndicator { pagesCount: 5; currentPage: 4 }
                    QtcPageIndicator { pagesCount: 5; currentPage: 2; enabled: false }
                    Item { Layout.fillWidth: true }

                    Label { text: qsTr("QtcIconDisplay"); font: Fonts.caption }
                    QtcIconDisplay {
                        iconSource: "image://qtcreator/utils/images/home.png?color=Token_Text_Default"
                        accessibleName: qsTr("Home")
                    }
                    QtcIconDisplay {
                        iconSource: "image://qtcreator/utils/images/settings.png?color=Token_Text_Default"
                        accessibleName: qsTr("Settings")
                    }
                    QtcIconDisplay {
                        iconSource: "image://qtcreator/utils/images/reload_gray.png?color=Token_Text_Default"
                        accessibleName: qsTr("Reload")
                    }
                    Item { Layout.fillWidth: true }
                    Item { Layout.fillWidth: true }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}

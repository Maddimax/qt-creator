// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// QML/JS code style. Three formatters, one shown at a time - which one is the
// container's business, not this file's; see QmlJSCodeStyleAspects.
AspectPage {
    id: root

    contentFillsHeight: true

    // The language's own settings, which the page reaches under one name
    // whatever the language called them. See CodeStyleAspect.
    readonly property var settings: AspectModels.named(aspects.Settings)
    readonly property var builtin: AspectModels.named(settings.BuiltinSettings)
    readonly property var tabs: AspectModels.named(builtin.TabSettings)
    readonly property var qmlformat: AspectModels.named(settings.QmlFormatSettings)
    readonly property var custom: AspectModels.named(settings.CustomSettings)

    CodeStyleSelector { aspects: root.aspects }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.fillWidth: true
            Layout.fillHeight: true
            // An even split, asked for rather than worked out from what is in
            // the two columns: a group's width would otherwise depend on the
            // split and the split on the group's width.
            Layout.preferredWidth: 1

            AspectGroupBox {
                title: qsTr("Formatter Selection")

                RadioGroupDelegate { aspect: root.settings.Formatter }
            }

            AspectGroupBox {
                title: qsTr("Built-in Formatter Settings")
                visible: root.settings.BuiltinSettings.visible
                enabled: root.settings.BuiltinSettings.enabled

                ColumnLayout {
                    spacing: Spacing.GapVS

                    TextDisplayDelegate { aspect: root.tabs.CodingStyleWarning }
                    BoolDelegate { aspect: root.tabs.AutoDetect }
                    SelectionDelegate { aspect: root.tabs.TabPolicy }
                    IntegerDelegate { aspect: root.tabs.IndentSize }
                    IntegerDelegate { aspect: root.tabs.TabSize }
                    SelectionDelegate { aspect: root.tabs.ContinuationAlignBehavior }
                    IntegerDelegate { aspect: root.builtin.LineLength }
                }
            }

            AspectGroupBox {
                title: qsTr("Global qmlformat Configuration")
                visible: root.settings.QmlFormatSettings.visible
                enabled: root.settings.QmlFormatSettings.enabled
                Layout.fillHeight: true

                ColumnLayout {
                    spacing: Spacing.GapVS

                    RowLayout {
                        spacing: Spacing.GapHM

                        TextDisplayDelegate { aspect: root.qmlformat.QmlFormatVersion }

                        Label {
                            text: "<a href='https://doc.qt.io/qt/qtqml-tooling-qmlformat.html'>"
                                  + qsTr("Open latest documentation") + "</a>"
                            textFormat: Text.RichText
                            onLinkActivated: (link) => Qt.openUrlExternally(link)
                        }

                        Item { Layout.fillWidth: true }
                    }

                    TableDelegate { aspect: root.qmlformat.QmlFormatOptions }

                    RowLayout {
                        spacing: Spacing.GapHXs

                        Item { Layout.fillWidth: true }
                        ButtonDelegate {
                            aspect: root.qmlformat.DeployIni
                            Layout.fillWidth: false
                        }
                        ButtonDelegate {
                            aspect: root.qmlformat.ResetOptions
                            Layout.fillWidth: false
                        }
                    }

                    Label {
                        text: qsTr("Global formatting options are ignored by projects having "
                                   + "their own deployed .qmlformat.ini files.")
                        wrapMode: Text.WordWrap
                        font: Fonts.body2
                        color: Tokens.textMuted
                        Layout.fillWidth: true
                    }
                }
            }

            AspectGroupBox {
                title: qsTr("Custom Formatter Configuration")
                visible: root.settings.CustomSettings.visible
                enabled: root.settings.CustomSettings.enabled

                ColumnLayout {
                    spacing: Spacing.GapVS

                    StringDelegate { aspect: root.custom.CustomFormatterPath }
                    StringDelegate { aspect: root.custom.CustomFormatterArguments }
                }
            }

            // The groups sit at the top; without this the column shares its
            // spare height out between them and one group floats.
            Item { Layout.fillHeight: true }
        }

        AspectGroupBox {
            title: qsTr("Preview")
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1

            CodeStylePreview { aspects: root.aspects }
        }
    }
}

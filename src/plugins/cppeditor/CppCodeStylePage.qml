// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// C++ code style. Six categories of settings, one shown at a time - which one
// is the container's business, not this file's, and it also decides which
// snippet the preview demonstrates; see CppCodeStyleAspects.
AspectPage {
    id: root

    contentFillsHeight: true

    readonly property var settings: AspectModels.named(aspects.Settings)
    readonly property var general: AspectModels.named(settings.GeneralSettings)
    readonly property var tabs: AspectModels.named(general.TabSettings)
    readonly property var indenting: AspectModels.named(settings.ContentSettings)
    readonly property var braces: AspectModels.named(settings.BracesSettings)
    readonly property var switches: AspectModels.named(settings.SwitchSettings)
    readonly property var alignment: AspectModels.named(settings.AlignmentSettings)
    readonly property var types: AspectModels.named(settings.TypesSettings)

    CodeStyleSelector { aspects: root.aspects }

    SelectionDelegate { aspect: root.settings.Category }

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
                title: qsTr("General")
                visible: root.settings.GeneralSettings.visible
                enabled: root.settings.GeneralSettings.enabled

                ColumnLayout {
                    spacing: Spacing.GapVS

                    TextDisplayDelegate { aspect: root.tabs.CodingStyleWarning }
                    BoolDelegate { aspect: root.tabs.AutoDetect }
                    SelectionDelegate { aspect: root.tabs.TabPolicy }
                    IntegerDelegate { aspect: root.tabs.IndentSize }
                    IntegerDelegate { aspect: root.tabs.TabSize }
                    SelectionDelegate { aspect: root.tabs.ContinuationAlignBehavior }
                    TextAreaDelegate { aspect: root.general.StatementMacros }
                }
            }

            AspectGroupBox {
                title: qsTr("Indent")
                visible: root.settings.ContentSettings.visible
                enabled: root.settings.ContentSettings.enabled

                ColumnLayout {
                    spacing: Spacing.GapVXs

                    BoolDelegate { aspect: root.indenting.IndentAccessSpecifiers }
                    BoolDelegate {
                        aspect: root.indenting.IndentDeclarationsRelativeToAccessSpecifiers
                    }
                    BoolDelegate { aspect: root.indenting.IndentFunctionBody }
                    BoolDelegate { aspect: root.indenting.IndentBlockBody }
                    BoolDelegate { aspect: root.indenting.IndentNamespaceBody }
                }
            }

            AspectGroupBox {
                title: qsTr("Indent Braces")
                visible: root.settings.BracesSettings.visible
                enabled: root.settings.BracesSettings.enabled

                ColumnLayout {
                    spacing: Spacing.GapVXs

                    BoolDelegate { aspect: root.braces.IndentClassBraces }
                    BoolDelegate { aspect: root.braces.IndentNamespaceBraces }
                    BoolDelegate { aspect: root.braces.IndentEnumBraces }
                    BoolDelegate { aspect: root.braces.IndentFunctionBraces }
                    BoolDelegate { aspect: root.braces.IndentBlockBraces }
                }
            }

            AspectGroupBox {
                title: qsTr("Indent within \"switch\"")
                visible: root.settings.SwitchSettings.visible
                enabled: root.settings.SwitchSettings.enabled

                ColumnLayout {
                    spacing: Spacing.GapVXs

                    BoolDelegate { aspect: root.switches.IndentSwitchLabels }
                    BoolDelegate { aspect: root.switches.IndentCaseStatements }
                    BoolDelegate { aspect: root.switches.IndentCaseBlocks }
                    BoolDelegate { aspect: root.switches.IndentCaseBreak }
                }
            }

            AspectGroupBox {
                title: qsTr("Align")
                visible: root.settings.AlignmentSettings.visible
                enabled: root.settings.AlignmentSettings.enabled

                ColumnLayout {
                    spacing: Spacing.GapVXs

                    BoolDelegate { aspect: root.alignment.AlignAssignments }
                    BoolDelegate { aspect: root.alignment.ExtraPaddingConditions }
                }
            }

            AspectGroupBox {
                title: qsTr("Bind '*' and '&&' in types/declarations to")
                visible: root.settings.TypesSettings.visible
                enabled: root.settings.TypesSettings.enabled

                ColumnLayout {
                    spacing: Spacing.GapVXs

                    BoolDelegate { aspect: root.types.BindStarToIdentifier }
                    BoolDelegate { aspect: root.types.BindStarToTypeName }
                    BoolDelegate { aspect: root.types.BindStarToLeftSpecifier }
                    BoolDelegate { aspect: root.types.BindStarToRightSpecifier }
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

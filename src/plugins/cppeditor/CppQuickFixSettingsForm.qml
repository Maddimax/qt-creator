// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The quick fix settings themselves, without a page around them: the global
// page and a project's panel show the same things and differ only in whose
// container they are shown for.
ColumnLayout {
    id: root

    // A NamedAspects for whichever container is being shown.
    required property var aspects

    readonly property var locations: AspectModels.named(root.aspects.Locations)
    readonly property var names: AspectModels.named(root.aspects.Names)
    readonly property var nameTest: AspectModels.named(root.aspects.NameTest)

    spacing: Spacing.GapVS
    Layout.fillWidth: true

    // A rule is a check box and a line count, which only mean anything
    // together, so the two aspects are drawn as one cell.
    component LineCountRule: RowLayout {
        id: rule

        required property var aspect
        readonly property var parts: AspectModels.named(rule.aspect)

        spacing: Spacing.GapHXs

        BoolDelegate {
            aspect: rule.parts.Use
            Layout.fillWidth: false
        }

        IntegerDelegate { aspect: rule.parts.Lines }
    }

    AspectGroupBox {
        title: qsTr("Generated Function Locations")

        ColumnLayout {
            spacing: Spacing.GapVXs

            GridLayout {
                columns: 2
                columnSpacing: Spacing.GapHXl
                rowSpacing: Spacing.GapVXs

                Label { text: qsTr("Generate Setters") }
                Label { text: qsTr("Generate Getters") }

                LineCountRule { aspect: root.locations.SetterOutsideClass }
                LineCountRule { aspect: root.locations.GetterOutsideClass }

                LineCountRule { aspect: root.locations.SetterInCppFile }
                LineCountRule { aspect: root.locations.GetterInCppFile }
            }

            Label {
                text: qsTr("Anything shorter is generated inside the class.")
                Layout.fillWidth: true
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Getter Setter Generation Properties")

        GridLayout {
            columns: 2
            columnSpacing: Spacing.GapHM
            rowSpacing: Spacing.GapVXs

            StringDelegate {
                aspect: root.names.GetterAttribute
                Layout.columnSpan: 2
            }

            StringDelegate { aspect: root.names.GetterName }
            TextDisplayDelegate { aspect: root.nameTest.Result0 }

            StringDelegate { aspect: root.names.SetterName }
            TextDisplayDelegate { aspect: root.nameTest.Result1 }

            StringDelegate { aspect: root.names.SetterParameterName }
            TextDisplayDelegate { aspect: root.nameTest.Result2 }

            BoolDelegate {
                aspect: root.names.SetterAsSlot
                Layout.columnSpan: 2
            }

            StringDelegate { aspect: root.names.ResetName }
            TextDisplayDelegate { aspect: root.nameTest.Result3 }

            StringDelegate { aspect: root.names.SignalName }
            TextDisplayDelegate { aspect: root.nameTest.Result4 }

            BoolDelegate {
                aspect: root.names.SignalWithNewValue
                Layout.columnSpan: 2
            }

            StringDelegate { aspect: root.names.MemberVariableName }
            TextDisplayDelegate { aspect: root.nameTest.Result5 }

            StringDelegate { aspect: root.names.NameFromMemberVariable }
            TextDisplayDelegate { aspect: root.nameTest.Result6 }

            RowLayout {
                Layout.columnSpan: 2
                spacing: Spacing.GapHM

                StringDelegate { aspect: root.nameTest.TestName }

                ButtonDelegate {
                    aspect: root.nameTest.RunTest
                    Layout.fillWidth: false
                }

                ButtonDelegate {
                    aspect: root.nameTest.HideTest
                    Layout.fillWidth: false
                }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Missing Namespace Handling")

        RadioGroupDelegate { aspect: root.aspects.NamespaceHandling }
    }

    BoolDelegate { aspect: root.aspects.UseAuto }

    AspectGroupBox {
        title: qsTr("Custom Getter Setter Templates")

        ColumnLayout {
            spacing: Spacing.GapVXs

            AspectListDelegate { aspect: root.aspects.CustomTemplates }

            Label {
                text: qsTr("Use <new> and <cur> to access the parameter and current value. "
                           + "Use <type> to access the type and <T> for the template parameter.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Value Types")

        StringListEditorDelegate { aspect: root.aspects.ValueTypes }
    }

    BoolDelegate { aspect: root.aspects.ReturnByConstRef }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    BoolDelegate { aspect: root.aspects.UseFakeVim }

    AspectGroupBox {
        title: qsTr("Vim Behavior")

        ColumnLayout {
            RowLayout {
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop

                    BoolDelegate { aspect: root.aspects.AutoIndent }
                    BoolDelegate { aspect: root.aspects.SmartIndent }
                    BoolDelegate { aspect: root.aspects.ExpandTab }
                    BoolDelegate { aspect: root.aspects.SmartTab }
                    BoolDelegate { aspect: root.aspects.UseEditorTabSettings }
                    BoolDelegate { aspect: root.aspects.MatchBracketsLikeVim }
                    BoolDelegate { aspect: root.aspects.HlSearch }
                    BoolDelegate { aspect: root.aspects.ShowCmd }
                    BoolDelegate { aspect: root.aspects.StartOfLine }
                    BoolDelegate { aspect: root.aspects.PassKeys }
                    BoolDelegate { aspect: root.aspects.BlinkingCursor }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignTop

                    BoolDelegate { aspect: root.aspects.IncSearch }
                    BoolDelegate { aspect: root.aspects.UseCoreSearch }
                    BoolDelegate { aspect: root.aspects.IgnoreCase }
                    BoolDelegate { aspect: root.aspects.SmartCase }
                    BoolDelegate { aspect: root.aspects.WrapScan }
                    BoolDelegate { aspect: root.aspects.ShowMarks }
                    BoolDelegate { aspect: root.aspects.PassControlKey }
                    BoolDelegate { aspect: root.aspects.CommaPassesShortcuts }
                    BoolDelegate { aspect: root.aspects.RelativeNumber }
                    BoolDelegate { aspect: root.aspects.CommandLineInEditor }
                    BoolDelegate { aspect: root.aspects.TildeOp }
                    BoolDelegate { aspect: root.aspects.Timeout }
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Spacing.GapHM

                IntegerDelegate { aspect: root.aspects.ShiftWidth }
                IntegerDelegate { aspect: root.aspects.TabStop }
                IntegerDelegate { aspect: root.aspects.ScrollOff }
                IntegerDelegate { aspect: root.aspects.TimeoutLen }
                IntegerDelegate { aspect: root.aspects.CursorFlashTime }
            }

            StringDelegate { aspect: root.aspects.Backspace }
            StringDelegate { aspect: root.aspects.IsKeyword }
            StringDelegate { aspect: root.aspects.TabOut }

            RowLayout {
                BoolDelegate { aspect: root.aspects.ReadVimRc }
                StringDelegate { aspect: root.aspects.VimRcPath }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Plugin Emulation")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.commentary }
            BoolDelegate { aspect: root.aspects.ReplaceWithRegister }
            BoolDelegate { aspect: root.aspects.argtextobj }
            BoolDelegate { aspect: root.aspects.exchange }
            BoolDelegate { aspect: root.aspects.surround }
            BoolDelegate { aspect: root.aspects.unimpaired }
        }
    }

    RowLayout {
        ButtonDelegate { aspect: root.aspects.CopyTextEditorSettings }
        ButtonDelegate { aspect: root.aspects.SetQtStyle }
        ButtonDelegate { aspect: root.aspects.SetPlainStyle }
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    BoolDelegate { aspect: aspects.UseFakeVim }

    AspectGroupBox {
        title: qsTr("Vim Behavior")

        ColumnLayout {
            RowLayout {
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop

                    BoolDelegate { aspect: aspects.AutoIndent }
                    BoolDelegate { aspect: aspects.SmartIndent }
                    BoolDelegate { aspect: aspects.ExpandTab }
                    BoolDelegate { aspect: aspects.SmartTab }
                    BoolDelegate { aspect: aspects.UseEditorTabSettings }
                    BoolDelegate { aspect: aspects.MatchBracketsLikeVim }
                    BoolDelegate { aspect: aspects.HlSearch }
                    BoolDelegate { aspect: aspects.ShowCmd }
                    BoolDelegate { aspect: aspects.StartOfLine }
                    BoolDelegate { aspect: aspects.PassKeys }
                    BoolDelegate { aspect: aspects.BlinkingCursor }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignTop

                    BoolDelegate { aspect: aspects.IncSearch }
                    BoolDelegate { aspect: aspects.UseCoreSearch }
                    BoolDelegate { aspect: aspects.IgnoreCase }
                    BoolDelegate { aspect: aspects.SmartCase }
                    BoolDelegate { aspect: aspects.WrapScan }
                    BoolDelegate { aspect: aspects.ShowMarks }
                    BoolDelegate { aspect: aspects.PassControlKey }
                    BoolDelegate { aspect: aspects.CommaPassesShortcuts }
                    BoolDelegate { aspect: aspects.RelativeNumber }
                    BoolDelegate { aspect: aspects.CommandLineInEditor }
                    BoolDelegate { aspect: aspects.TildeOp }
                    BoolDelegate { aspect: aspects.Timeout }
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Spacing.GapHM

                IntegerDelegate { aspect: aspects.ShiftWidth }
                IntegerDelegate { aspect: aspects.TabStop }
                IntegerDelegate { aspect: aspects.ScrollOff }
                IntegerDelegate { aspect: aspects.TimeoutLen }
                IntegerDelegate { aspect: aspects.CursorFlashTime }
            }

            StringDelegate { aspect: aspects.Backspace }
            StringDelegate { aspect: aspects.IsKeyword }
            StringDelegate { aspect: aspects.TabOut }

            RowLayout {
                BoolDelegate { aspect: aspects.ReadVimRc }
                StringDelegate { aspect: aspects.VimRcPath }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Plugin Emulation")

        ColumnLayout {
            BoolDelegate { aspect: aspects.commentary }
            BoolDelegate { aspect: aspects.ReplaceWithRegister }
            BoolDelegate { aspect: aspects.argtextobj }
            BoolDelegate { aspect: aspects.exchange }
            BoolDelegate { aspect: aspects.surround }
            BoolDelegate { aspect: aspects.unimpaired }
        }
    }

    RowLayout {
        ButtonDelegate { aspect: aspects.CopyTextEditorSettings }
        ButtonDelegate { aspect: aspects.SetQtStyle }
        ButtonDelegate { aspect: aspects.SetPlainStyle }
    }
}

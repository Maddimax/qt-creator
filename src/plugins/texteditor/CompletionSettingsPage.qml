// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    AspectGroupBox {
        title: qsTr("Behavior")

        ColumnLayout {
            SelectionDelegate { aspect: aspects.CaseSensitivity }
            SelectionDelegate { aspect: aspects.CompletionTrigger }
            IntegerDelegate { aspect: aspects.AutomaticProposalTimeout }
            IntegerDelegate { aspect: aspects.CharacterThreshold }
            BoolDelegate { aspect: aspects.PartiallyComplete }
            BoolDelegate { aspect: aspects.AutoSplitStrings }
        }
    }

    AspectGroupBox {
        title: qsTr("Automatically Insert Matching Characters")

        RowLayout {
            ColumnLayout {
                Layout.alignment: Qt.AlignTop

                BoolDelegate { aspect: aspects.AutoInsertBraces }
                BoolDelegate { aspect: aspects.SurroundingAutoBrackets }
                BoolDelegate { aspect: aspects.SpaceAfterFunctionName }
                BoolDelegate { aspect: aspects.HighlightAutoComplete }

                BoolDelegate {
                    aspect: aspects.SkipAutoComplete
                    Layout.leftMargin: Spacing.PaddingHL
                }

                BoolDelegate {
                    aspect: aspects.AutoRemove
                    Layout.leftMargin: Spacing.PaddingHL
                }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignTop

                BoolDelegate { aspect: aspects.AutoInsertQuotes }
                BoolDelegate { aspect: aspects.SurroundingAutoQuotes }
                BoolDelegate { aspect: aspects.AnimateAutoComplete }
                BoolDelegate { aspect: aspects.OverwriteClosingChars }
            }
        }
    }
}

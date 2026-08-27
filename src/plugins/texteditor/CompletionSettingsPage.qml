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

    AspectGroupBox {
        title: qsTr("Behavior")

        ColumnLayout {
            SelectionDelegate { aspect: root.aspects.CaseSensitivity }
            SelectionDelegate { aspect: root.aspects.CompletionTrigger }
            IntegerDelegate { aspect: root.aspects.AutomaticProposalTimeout }
            IntegerDelegate { aspect: root.aspects.CharacterThreshold }
            BoolDelegate { aspect: root.aspects.PartiallyComplete }
            BoolDelegate { aspect: root.aspects.AutoSplitStrings }
        }
    }

    AspectGroupBox {
        title: qsTr("Automatically Insert Matching Characters")

        RowLayout {
            ColumnLayout {
                Layout.alignment: Qt.AlignTop

                BoolDelegate { aspect: root.aspects.AutoInsertBraces }
                BoolDelegate { aspect: root.aspects.SurroundingAutoBrackets }
                BoolDelegate { aspect: root.aspects.SpaceAfterFunctionName }
                BoolDelegate { aspect: root.aspects.HighlightAutoComplete }

                BoolDelegate {
                    aspect: root.aspects.SkipAutoComplete
                    Layout.leftMargin: Spacing.PaddingHL
                }

                BoolDelegate {
                    aspect: root.aspects.AutoRemove
                    Layout.leftMargin: Spacing.PaddingHL
                }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignTop

                BoolDelegate { aspect: root.aspects.AutoInsertQuotes }
                BoolDelegate { aspect: root.aspects.SurroundingAutoQuotes }
                BoolDelegate { aspect: root.aspects.AnimateAutoComplete }
                BoolDelegate { aspect: root.aspects.OverwriteClosingChars }
            }
        }
    }
}

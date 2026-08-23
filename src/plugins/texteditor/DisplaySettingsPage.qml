// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Two settings objects share this page, so their aspects come through
// AspectModels.named() rather than through the page's own "aspects".
AspectPage {
    id: root

    readonly property var display: AspectModels.named(aspects.Display)
    readonly property var margin: AspectModels.named(aspects.Margin)

    AspectGroupBox {
        title: qsTr("Margin")

        ColumnLayout {
            RowLayout {
                BoolDelegate { aspect: root.margin.ShowMargin }
                IntegerDelegate { aspect: root.margin.MarginColumn }
                BoolDelegate { aspect: root.margin.tintMarginArea }
            }

            BoolDelegate { aspect: root.margin.UseIndenter }
            IntegerDelegate { aspect: root.margin.centeredEditorContentWidthPercent }
        }
    }

    AspectGroupBox {
        title: qsTr("Wrapping")

        ColumnLayout {
            BoolDelegate { aspect: root.display.TextWrapping }
            TextDisplayDelegate { aspect: root.display.LineSpacingNote }

            RowLayout {
                BoolDelegate { aspect: root.display.BreakIndent }
                IntegerDelegate { aspect: root.display.BreakIndentMin }
                IntegerDelegate { aspect: root.display.BreakIndentShift }
            }

            StringDelegate { aspect: root.display.ShowBreak }
            BoolDelegate { aspect: root.display.BreakIndentSbr }
        }
    }

    AspectGroupBox {
        title: qsTr("Display")

        RowLayout {
            ColumnLayout {
                Layout.alignment: Qt.AlignTop

                BoolDelegate { aspect: root.display.DisplayLineNumbers }
                BoolDelegate { aspect: root.display.DisplayFoldingMarkers }
                BoolDelegate { aspect: root.display.MarkTextChanges }
                BoolDelegate { aspect: root.display.VisualizeWhitespace }
                BoolDelegate { aspect: root.display.CenterCursorOnScroll }
                BoolDelegate { aspect: root.display.AutoFoldFirstComment }
                BoolDelegate { aspect: root.display.ScrollBarHighlights }
                BoolDelegate { aspect: root.display.AnimateNavigationWithinFile }
                BoolDelegate { aspect: root.display.HighlightSelection }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignTop

                BoolDelegate { aspect: root.display.HighlightCurrentLine2Key }
                BoolDelegate { aspect: root.display.HighlightBlocksKey }
                BoolDelegate { aspect: root.display.AnimateMatchingParenthesesKey }
                BoolDelegate { aspect: root.display.VisualizeIndent }
                BoolDelegate { aspect: root.display.HightlightMatchingParenthesesKey }
                BoolDelegate { aspect: root.display.OpenLinksInNextSplitKey }
                BoolDelegate { aspect: root.display.DisplayFileEncoding }
                BoolDelegate { aspect: root.display.DisplayFileLineEnding }
                BoolDelegate { aspect: root.display.DisplayTabSettings }
                BoolDelegate { aspect: root.display.DisplayMinimap }
                BoolDelegate { aspect: root.display.MarkDiffChangeSigns }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Line Annotations")
        checkAspect: root.display.DisplayAnnotations

        SelectionDelegate { aspect: root.display.AnnotationAlignment }
    }
}

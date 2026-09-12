// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The whole Search Results pane: the row that says what is being looked for,
// and the results underneath it. One scene rather than two, because a pane
// with a Qt Quick strip above a Qt Quick tree would be two live scenes where
// one will do.
ColumnLayout {
    id: root

    // Handed over before the source is set, by whichever front end hosts this.
    required property var controller

    objectName: "searchResultsPane"
    spacing: 0

    SearchResultsRow {
        controller: root.controller
        Layout.fillWidth: true
        Layout.leftMargin: Spacing.PaddingHXs
        Layout.rightMargin: Spacing.PaddingHXs
        Layout.topMargin: Spacing.PaddingVXxs
    }

    TreeView {
        id: results

        objectName: "searchResultsTree"
        // What the scene gives the keyboard to when the pane is focused: a
        // QQuickWidget hands focus to its scene, and a scene with no focus
        // item hands it nowhere.
        focus: true
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.controller.results
        boundsBehavior: Flickable.StopAtBounds
        selectionBehavior: TableView.SelectRows
        selectionModel: ItemSelectionModel { model: results.model }

        ScrollBar.vertical: ScrollBar {}

        delegate: TreeViewDelegate {
            id: result

            // Only what this adds: TreeViewDelegate already requires row,
            // model and text, and redeclaring one of those shadows the base's,
            // which the view then never initialises.
            required property int column
            required property var drawnText
            required property var drawnHighlightStart
            required property var drawnHighlightLength
            required property var drawnFunctionText
            required property var checkState
            required property var isGroupingItem

            implicitHeight: Metrics.tableRowMinimumHeight

            onDoubleClicked: root.controller.activate(results.index(result.row, result.column))

            // A file group has no match to tick; the rows under it do, and
            // only while a replace is being set up - which is what the model
            // says by offering a check state at all.
            indicator: CheckBox {
                objectName: "resultCheck"
                visible: !result.isGroupingItem && result.checkState !== undefined
                checked: result.checkState === Qt.Checked
                onToggled: root.controller.setChecked(
                               results.index(result.row, result.column), checked)
            }

            contentItem: RowLayout {
                spacing: Spacing.GapHXs

                // The three pieces the line is cut into. The offsets are into
                // the drawn text, tabs already expanded, so nothing here has
                // to know what a tab is worth.
                Text {
                    objectName: "resultBefore"
                    text: result.drawnHighlightStart > 0
                          ? result.drawnText.substring(0, result.drawnHighlightStart) : ""
                    visible: text !== ""
                    color: Tokens.textDefault
                    font: Fonts.fixed
                    textFormat: Text.PlainText
                }

                Text {
                    objectName: "resultMatch"
                    text: result.drawnHighlightLength > 0
                          ? result.drawnText.substring(
                                result.drawnHighlightStart,
                                result.drawnHighlightStart + result.drawnHighlightLength)
                          : ""
                    visible: text !== ""
                    color: Tokens.textDefault
                    font: Fonts.fixed
                    textFormat: Text.PlainText

                    Rectangle {
                        anchors.fill: parent
                        z: -1
                        color: Tokens.notificationNeutralMuted
                        radius: Spacing.RadiusS
                    }
                }

                Text {
                    objectName: "resultAfter"
                    text: result.drawnHighlightStart < 0
                          ? result.drawnText
                          : result.drawnText.substring(result.drawnHighlightStart
                                                       + result.drawnHighlightLength)
                    visible: text !== ""
                    color: Tokens.textDefault
                    font: Fonts.fixed
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    objectName: "resultFunction"
                    text: result.drawnFunctionText
                    visible: text !== ""
                    color: Tokens.textMuted
                    font: Fonts.fixed
                    textFormat: Text.PlainText
                }
            }
        }
    }
}

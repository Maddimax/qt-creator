// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The to-do entries a scan found: what was written, where, and on which line.
// Which row is current, what the list is ordered by and what opening one does
// all belong to the pane - a view that kept them was why nothing could say
// where the list was without drawing it.
ColumnLayout {
    id: root

    // Todo::Internal::TodoOutputPane.
    required property var pane

    spacing: 0

    HorizontalHeaderView {
        id: header

        objectName: "todoHeader"
        syncView: view
        clip: true
        Layout.fillWidth: true

        // Clicking a heading orders by it, and clicking it again turns the
        // order round, which is what the tree view's sort indicator did.
        property int sortColumn: 0
        property bool ascending: true

        delegate: Rectangle {
            id: heading

            required property int index
            required property string display

            implicitWidth: 100
            implicitHeight: label.implicitHeight + 2 * Spacing.PaddingVXs
            color: Tokens.backgroundSubtle

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Spacing.PaddingHS
                anchors.rightMargin: Spacing.PaddingHS
                spacing: Spacing.GapHXs

                Label {
                    id: label

                    text: heading.display
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                // Which way round the order is, on the column it applies to.
                Label {
                    text: header.ascending ? "▲" : "▼"
                    visible: header.sortColumn === heading.index
                    color: Tokens.textMuted
                    font: Fonts.caption
                }
            }

            TapHandler {
                onTapped: {
                    if (header.sortColumn === heading.index)
                        header.ascending = !header.ascending
                    else
                        header.ascending = true
                    header.sortColumn = heading.index
                    root.pane.sortBy(header.sortColumn, header.ascending)
                }
            }
        }
    }

    TableView {
        id: view

        objectName: "todoList"
        model: root.pane.rows
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Layout.fillWidth: true
        Layout.fillHeight: true

        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        // The text column takes what the other two do not: a to-do is mostly
        // its text, and the file it is in is read from its right-hand end.
        columnWidthProvider: function (column) {
            if (column === 2)
                return Metrics.lineEditWidth / 2
            if (column === 1)
                return Math.max(Metrics.lineEditWidth, view.width / 3)
            return Math.max(Metrics.lineEditWidth,
                            view.width - Math.max(Metrics.lineEditWidth, view.width / 3)
                                - Metrics.lineEditWidth / 2)
        }
        onWidthChanged: Qt.callLater(view.forceLayout)

        delegate: Rectangle {
            id: cell

            required property int row
            required property int column
            required property string display
            required property var decoration
            required property var foreground

            implicitHeight: Math.max(Metrics.tableRowMinimumHeight,
                                     text.implicitHeight + 2 * Spacing.PaddingVXs)
            color: cell.row === root.pane.currentRow ? Tokens.backgroundMuted : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Spacing.PaddingHS
                anchors.rightMargin: Spacing.PaddingHS
                spacing: Spacing.GapHXs

                // Only the first column carries one, which is where the model
                // puts it.
                Image {
                    source: AspectModels.decorationUrl(cell.decoration ?? undefined)
                    visible: source.toString() !== ""
                    sourceSize.width: Metrics.listRowIconSize
                    sourceSize.height: Metrics.listRowIconSize
                    Layout.preferredWidth: visible ? Metrics.listRowIconSize : 0
                    Layout.preferredHeight: Metrics.listRowIconSize
                }

                Label {
                    id: text

                    text: cell.display
                    color: cell.foreground ?? Tokens.textDefault
                    // A path is read from its end, so that is the end that
                    // stays when there is not room for it.
                    elide: cell.column === 1 ? Text.ElideLeft : Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            TapHandler {
                onTapped: root.pane.setCurrentRow(cell.row)
                onDoubleTapped: {
                    root.pane.setCurrentRow(cell.row)
                    root.pane.activateRow(cell.row)
                }
            }
        }
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A tree of what an aspect has to show, from the model it hands out. Read-only:
// this is for values a page reports rather than ones it lets the user set - a
// qbs profile's properties, say. Editing lives in TableDelegate, and a list
// with groups and buttons in GroupedListDelegate.
RowLayout {
    id: root

    required property Aspect aspect
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    // The aspect owns the model, so it outlives any one page.
    readonly property var treeModel: aspect?.tableModel() ?? null

    // A page showing a tree usually wants to offer these, and they are the
    // view's business rather than the aspect's.
    function expandAll(): void { view.expandRecursively(-1, -1) }
    function collapseAll(): void { view.collapseRecursively() }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true
    Layout.fillHeight: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignTop
    }

    Frame {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Metrics.formListHeight
        Layout.preferredHeight: Metrics.formListHeight

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            HorizontalHeaderView {
                syncView: view
                clip: true
                Layout.fillWidth: true
            }

            TreeView {
                id: view

                objectName: "aspectTree"
                model: root.treeModel
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                selectionBehavior: TableView.SelectRows
                selectionMode: TableView.SingleSelection
                selectionModel: ItemSelectionModel { model: view.model }
                ToolTip.text: root.toolTip
                ToolTip.visible: false
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Every column as wide as its contents want, up to a cap, and
                // the last one takes what is left; the same rule TableDelegate
                // follows, and for the same reason - a column wider than the
                // view scrolls its header label out of sight.
                columnWidthProvider: function (column) {
                    const capped = function (i) {
                        return Math.min(view.implicitColumnWidth(i), Metrics.tableColumnMaxWidth)
                    }
                    if (column < view.columns - 1)
                        return capped(column)
                    let used = 0
                    for (let i = 0; i < view.columns - 1; ++i)
                        used += capped(i)
                    return Math.max(Metrics.lineEditWidth, view.width - used)
                }
                onWidthChanged: Qt.callLater(view.forceLayout)

                ScrollBar.vertical: ScrollBar {}

                delegate: TreeViewDelegate {
                    id: cell

                    required property var model
                    required property int column

                    implicitWidth: Math.max(Metrics.lineEditWidth, implicitContentWidth)
                    implicitHeight: Metrics.tableRowMinimumHeight
                    // Only the first column carries the indent and the branch
                    // handle; the rest line up with the header.
                    indentation: cell.column === 0 ? 12 : 0
                    text: cell.model.display ?? ""
                    ToolTip.text: cell.model.toolTip ?? ""
                    ToolTip.visible: cell.hovered && ToolTip.text !== ""
                }
            }
        }
    }
}

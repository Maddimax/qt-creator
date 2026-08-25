// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A tree of what an aspect has to show, from the model it hands out. The cells
// are TableDelegate's, so a tree can be written to where the model says so -
// the Locator's prefixes are edited in one. A list with groups and buttons is
// GroupedListDelegate.
RowLayout {
    id: root

    required property Aspect aspect
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool editable: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)

    property var pres: aspect ? AspectModels.presentation(aspect) : ({})

    // The aspect owns the model, so it outlives any one page.
    readonly property var treeModel: aspect ? AspectModels.tableModel(aspect) : null

    // Narrowing the rows is the view's business; which rows there are is the
    // model's. Always in the chain, so there is one index space whether or not
    // the filter field is shown. A branch is kept when anything under it
    // matches, which is what makes a filter useful on a tree.
    readonly property TreeFilterModel rows: TreeFilterModel {
        sourceModel: root.treeModel
    }

    // Which row is being looked at, in the aspect's own model rather than the
    // filtered one: a page showing a detail of it - the expression a command
    // is mapped to - looks the row up in the model it owns, and an index from
    // the proxy finds nothing there. An invalid index means nothing is picked.
    readonly property var currentIndex: {
        const index = view.selectionModel.currentIndex
        return index && index.valid ? root.rows.mapToSource(index) : index
    }

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

            // Above the tree and out of the way of it, the way a search field
            // sits over a list. No filter where the aspect names no
            // placeholder for it.
            TextField {
                id: filter

                objectName: "treeFilterField"
                placeholderText: root.pres.filterPlaceholderText ?? ""
                visible: placeholderText !== ""
                enabled: root.aspect?.enabled ?? false
                Layout.fillWidth: true
                onTextChanged: root.rows.setFilterFixedString(text)
            }

            HorizontalHeaderView {
                syncView: view
                clip: true
                Layout.fillWidth: true
            }

            TreeView {
                id: view

                objectName: "aspectTree"
                model: root.rows
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

                    // Only the column: TreeViewDelegate already requires row
                    // and model, and redeclaring one of those shadows the
                    // base's, which the view then never initialises - the
                    // delegate fails to incubate and no row is drawn at all.
                    required property int column

                    implicitWidth: Math.max(Metrics.lineEditWidth, implicitContentWidth)
                    implicitHeight: Math.max(Metrics.tableRowMinimumHeight,
                                             implicitContentHeight)
                    // Only the first column carries the indent and the branch
                    // handle; the rest line up with the header.
                    indentation: cell.column === 0 ? 12 : 0
                    // Not drawn - the content item below is - but read out,
                    // and the delegate's own text is what an accessible name
                    // comes from.
                    text: cell.model.display ?? ""
                    // TreeViewDelegate's own content item binds straight to
                    // model.display, which is undefined for a cell the model
                    // says nothing about - a group heading has no second
                    // column. The shared cell asks the model what it is
                    // instead, so an editable one is a field and a checkable
                    // one a check box.
                    contentItem: AspectTableCell {
                        model: cell.model
                        editable: root.editable
                        // A tree reports what the page found unless its model
                        // says a cell may be written to.
                        editableByDefault: false
                    }
                }
            }
        }
    }
}

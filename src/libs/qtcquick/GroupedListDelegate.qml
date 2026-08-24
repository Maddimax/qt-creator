// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Items in named groups - what was found and what the user added - with one of
// them current, and Clone, Remove and Make Default acting on it. Which item
// that is and what may be done to it is the aspect's answer, not this file's:
// it is the same answer for the QTreeView these pages used. See
// Utils::GroupedListAspect.
RowLayout {
    id: root

    required property Aspect aspect
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool editable: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)

    // The groups with their items under them. The aspect owns it, so it
    // outlives any one page.
    readonly property var treeModel: aspect?.displayModel() ?? null

    // A page that shows the current item's settings beside the tree asks the
    // aspect, not this. Kept here only to move the view's selection when
    // something else - Add, Clone, a removal moving on - changes it.
    Connections {
        target: root.aspect

        function onCurrentRowChanged(oldRow: int, newRow: int): void {
            root.showRow(newRow)
        }
    }

    function showRow(row: int): void {
        const index = root.aspect.indexForRow(row)
        if (!index.valid) {
            view.selectionModel.clearCurrentIndex()
            return
        }
        // A row in a collapsed group cannot be current, and the groups are
        // expanded anyway - but a clone lands in a group that may not exist
        // until it does.
        view.expandToIndex(index)
        const cell = view.cellAtIndex(index)
        if (cell.y >= 0)
            view.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect)
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        spacing: Spacing.GapVXs
        Layout.fillWidth: true
        Layout.fillHeight: true

        Label {
            text: root.labelText
            visible: text !== ""
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

                    objectName: "groupedListTree"
                    model: root.treeModel
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    selectionBehavior: TableView.SelectRows
                    // One at a time: the three buttons act on the current item
                    // and there is no meaning to more than one.
                    selectionMode: TableView.SingleSelection
                    selectionModel: ItemSelectionModel { model: view.model }
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // The groups are the structure, not something to open and
                    // close: the widget tree hid the branch handles and
                    // expanded everything.
                    onModelChanged: Qt.callLater(view.expandRecursively, -1, -1)
                    Component.onCompleted: view.expandRecursively(-1, -1)

                    columnWidthProvider: function (column) {
                        const capped = function (i) {
                            return Math.min(view.implicitColumnWidth(i),
                                            Metrics.tableColumnMaxWidth)
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
                        required property int row
                        required property int column

                        // A group heading is not an item: it has no source row
                        // and nothing acts on it.
                        readonly property bool isItem:
                            root.aspect.rowForIndex(view.index(cell.row, cell.column)) >= 0

                        implicitWidth: Math.max(Metrics.lineEditWidth, implicitContentWidth)
                        implicitHeight: Metrics.tableRowMinimumHeight
                        // Only the first column carries the indent and the
                        // branch handle; the rest line up with the header.
                        indentation: cell.column === 0 ? 12 : 0
                        text: cell.model.display ?? ""
                        font.bold: !cell.isItem
                        ToolTip.text: cell.model.toolTip ?? ""
                        ToolTip.visible: cell.hovered && ToolTip.text !== ""

                        // Clicking a group heading would otherwise leave the
                        // buttons acting on whatever was current before.
                        onClicked: {
                            if (cell.isItem) {
                                root.aspect.currentRow =
                                    root.aspect.rowForIndex(view.index(cell.row, cell.column))
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        spacing: Spacing.GapVXs
        Layout.alignment: Qt.AlignTop

        // Add is the page's, not the list's: what a new item is made of is
        // the page's business. It goes above these in the page's own QML.
        Button {
            objectName: "groupedListCloneButton"
            text: qsTr("Clone")
            enabled: root.editable && (root.aspect?.canClone ?? false)
            onClicked: root.aspect.cloneCurrent()
        }

        Button {
            objectName: "groupedListRemoveButton"
            // Removal is not applied until the page is, so it is undone
            // rather than done twice.
            text: (root.aspect?.currentIsRemoved ?? false) ? qsTr("Restore") : qsTr("Remove")
            enabled: root.editable && (root.aspect?.canRemove ?? false)
            onClicked: root.aspect.removeCurrent()
        }

        Button {
            objectName: "groupedListMakeDefaultButton"
            text: qsTr("Make Default")
            visible: root.pres.showsDefault ?? false
            enabled: root.editable && (root.aspect?.canMakeDefault ?? false)
            ToolTip.text: root.toolTip
            ToolTip.visible: hovered && root.toolTip !== ""
            onClicked: root.aspect.makeCurrentDefault()
        }

        Item { Layout.fillHeight: true }
    }
}

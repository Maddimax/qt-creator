// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Rows and columns, from the model the aspect hands out. What a cell is - a
// check box, a choice, a field, or text to read - is the model's answer,
// because it is the same answer for a QTableView and it usually depends on the
// row's other cells. See Utils::AspectTable.
RowLayout {
    id: root

    required property Aspect aspect
    // The descriptor, read from the aspect rather than taken as model roles, so
    // that a hand-written page can use this delegate with nothing but the
    // aspect. See AspectModels::presentation().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool editable: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)

    // The aspect owns the model, so it outlives any one page.
    readonly property var sourceModel: aspect ? AspectModels.tableModel(aspect) : null

    // Which row is selected, in the aspect's own model rather than the filtered
    // one - a page showing a detail of the current row means that row, not the
    // one at that position in whatever is on screen. -1 when nothing is.
    // TableView answers currentRow and currentColumn; it has no currentIndex,
    // and reading one gave undefined - so this was -1 whatever was selected,
    // and every page that shows a detail of the current row was told nothing.
    readonly property int currentRow: {
        const row = view.currentRow
        if (row < 0)
            return -1
        return root.rows.mapToSource(root.rows.index(row, 0)).row
    }

    // Put the keyboard on a row, the way a page that opens on its first entry
    // does. The row is one of the aspect's, not of whatever the filter is
    // showing.
    function selectRow(row: int): void {
        const source = root.sourceModel
        if (!source || row < 0 || row >= source.rowCount())
            return
        const mapped = root.rows.mapFromSource(source.index(row, 0))
        if (!mapped.valid)
            return
        view.selectionModel.setCurrentIndex(mapped, ItemSelectionModel.ClearAndSelect)
    }

    // Every selected row, in the aspect's own model, ascending. A page that
    // acts on a selection - export these, remove those - wants all of them and
    // not just the one the keyboard is on.
    readonly property var selectedRows: {
        const rows = []
        const indexes = view.selectionModel.selectedIndexes
        for (let i = 0; i < indexes.length; ++i) {
            const row = root.rows.mapToSource(indexes[i]).row
            if (!rows.includes(row))
                rows.push(row)
        }
        rows.sort((a, b) => a - b)
        return rows
    }

    // Narrowing the rows is the view's business; which rows there are and what
    // they offer is the model's. Always in the chain, so there is one index
    // space whether or not the filter field is shown.
    readonly property TableFilterModel rows: TableFilterModel {
        sourceModel: root.sourceModel
    }

    // A row was chosen and acted on - the widget views these replace called it
    // "activated" and reached it by double click or by Return. A page that
    // opens something, or a dialog that accepts, listens for this; the row is
    // one of the aspect's own.
    signal rowActivated(row: int)

    // Which column the rows are sorted by, and which way. Starts at what the
    // aspect asked for; the reader changes it by clicking a heading, as they
    // did in the widget view. -1 is the model's own order.
    property int sortColumn: root.pres.sortColumn ?? -1
    property bool sortAscending: root.pres.sortAscending ?? true

    // Sorting the proxy rather than the model: the order is this view's, and
    // the aspect's rows keep the order they were given - which is the index
    // space currentRow and selectedRows report in.
    function applySorting(): void {
        if (root.sortColumn < 0)
            root.rows.sort(-1)
        else
            root.rows.sort(root.sortColumn, root.sortAscending ? Qt.AscendingOrder : Qt.DescendingOrder)
    }

    // A heading was clicked: the column it names becomes the one sorted by, and
    // clicking the one already sorted by turns it around.
    function sortBy(column: int): void {
        if (column === root.sortColumn)
            root.sortAscending = !root.sortAscending
        else
            root.sortColumn = column
    }

    onSortColumnChanged: root.applySorting()
    onSortAscendingChanged: root.applySorting()
    Component.onCompleted: root.applySorting()

    Connections {
        target: root.aspect
        function onControlConfigurationChanged() {
            root.pres = AspectModels.presentation(root.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true
    // A table is the tall thing on a page, so it takes the room the page has
    // rather than sitting in a box the height of a list editor.
    Layout.fillHeight: true

    FormLabel {
        text: root.labelText
        Layout.alignment: Qt.AlignTop
    }

    ColumnLayout {
        id: column

        spacing: Spacing.GapVXs
        Layout.fillWidth: true
        Layout.fillHeight: true

        // Above the table and out of the way of it, the way a search field sits
        // over a list.
        RowLayout {
            spacing: 0
            visible: filter.placeholderText !== ""
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            TextField {
                id: filter

                objectName: "tableFilterField"
                placeholderText: root.pres.filterPlaceholderText ?? ""
                enabled: root.aspect?.enabled ?? false
                Layout.preferredWidth: Math.round(column.width / 4)
                onTextChanged: root.rows.setFilterFixedString(text)
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Metrics.formListHeight
            // Asked for, rather than however tall the rows happen to add up
            // to: a table with more rows than fit scrolls, and does not push
            // the buttons under it off the bottom of the page.
            Layout.preferredHeight: Metrics.formListHeight

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Built only where there are column names to put in it. A
                // one-column list has none, and an empty header bar is neither
                // what the widget view showed nor something the style's own
                // heading copes with - hiding it is not enough, because the
                // heading is incubated first and warns on the way.
                Loader {
                    active: AspectModels.namesItsColumns(root.sourceModel)
                    Layout.fillWidth: true

                    sourceComponent: HorizontalHeaderView {
                        id: header

                        syncView: view
                        clip: true

                        // The style's own heading, with two things added: it
                        // takes the click that changes what the rows are
                        // sorted by, and it says when it is the column they
                        // are sorted by. Subclassed rather than replaced so
                        // that a heading still looks like the platform's.
                        delegate: HorizontalHeaderViewDelegate {
                            id: heading

                            required property int index

                            objectName: "tableHeaderSection"

                            TapHandler {
                                onTapped: root.sortBy(heading.index)
                            }

                            Image {
                                objectName: "tableSortIndicator"
                                anchors.right: parent.right
                                anchors.rightMargin: Spacing.PaddingHS
                                anchors.verticalCenter: parent.verticalCenter
                                visible: root.sortColumn === heading.index
                                fillMode: Image.Pad
                                source: "image://qtcreator/utils/images/"
                                        + (root.sortAscending ? "arrowup.png" : "arrowdown.png")
                                        + "?color=Token_Text_Muted"
                            }
                        }
                    }
                }

                // The background the rows are read against, where the aspect
                // says they have one: a list of syntax formats is showing what
                // it describes, and a format that sets no background of its own
                // is still meant to be read on the editor's background rather
                // than on the form's. Behind the rows only - the header keeps
                // the form's colours, as it did in the widget view.
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Rectangle {
                        anchors.fill: parent
                        visible: root.pres.rowBackground !== undefined
                        color: root.pres.rowBackground ?? "transparent"
                    }

                    TableView {
                        id: view

                        anchors.fill: parent
                        model: root.rows
                        reuseItems: false
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        selectionBehavior: TableView.SelectRows
                        // More than one at a time: the widget tables these replace
                        // used ExtendedSelection, and the pages that act on a
                        // selection were written for it.
                        selectionMode: TableView.ExtendedSelection
                        selectionModel: ItemSelectionModel { model: view.model }

                        // Return on the row the keyboard is on, which is the
                        // other half of what a widget view called activation.
                        Keys.onPressed: (event) => {
                            if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter)
                                return
                            if (root.currentRow < 0)
                                return
                            root.rowActivated(root.currentRow)
                            event.accepted = true
                        }

                        // Double click, the usual half. A passive handler, so
                        // that the single clicks the view selects with are
                        // untouched.
                        TapHandler {
                            objectName: "tableActivateTap"
                            acceptedButtons: Qt.LeftButton
                            gesturePolicy: TapHandler.DragThreshold

                            onDoubleTapped: (eventPoint) => {
                                const cell = view.cellAtPosition(eventPoint.position, true)
                                if (cell.y < 0)
                                    return
                                const mapped = root.rows.mapToSource(
                                    root.rows.index(cell.y, 0)).row
                                if (mapped >= 0)
                                    root.rowActivated(mapped)
                            }
                        }
                        ToolTip.text: root.toolTip
                        ToolTip.visible: false
                        // Each column as wide as it needs to be, and the last one
                        // takes what is left - the way the widget table stretched
                        // its last header section. Dividing the width evenly gave a
                        // check box as much room as a description.
                        // Every column as wide as its contents want, up to a cap,
                        // and the last one takes what is left. Without the cap a
                        // long description made its column wider than the table,
                        // which scrolled the centred header label out of the
                        // clipped header - the column looked unnamed.
                        // A function literal is what TableView's own documentation assigns
                        // here, and the property is a QJSValue. qmllint wants a type it has
                        // no way to name: annotating the signature turns two complaints into
                        // one about QJSValue itself.
                        // qmllint disable Quick.unexpected-var-type
                        columnWidthProvider: function (column) {
                            // What every column but the last wants. They get
                            // it when the table can hold it - the widget header
                            // sized each column to its contents and stretched
                            // the last - and are capped only when it cannot,
                            // which is the case the cap was written for.
                            let asked = 0
                            for (let i = 0; i < view.columns - 1; ++i)
                                asked += view.implicitColumnWidth(i)
                            const fits = asked + Metrics.lineEditWidth <= view.width
                            const capped = function (i) {
                                return fits ? view.implicitColumnWidth(i)
                                            : Math.min(view.implicitColumnWidth(i),
                                                       Metrics.tableColumnMaxWidth)
                            }
                            if (column < view.columns - 1)
                                return capped(column)
                            let used = 0
                            for (let i = 0; i < view.columns - 1; ++i)
                                used += capped(i)
                            return Math.max(Metrics.lineEditWidth, view.width - used)
                        }
                        // qmllint enable Quick.unexpected-var-type
                        // A width the provider already answered for is cached, so
                        // the last column has to be asked again when the table is
                        // resized.
                        onWidthChanged: Qt.callLater(view.forceLayout)

                        ScrollBar.vertical: ScrollBar {}

                        delegate: AspectTableCell {
                            // Filled by the view because they are required
                            // here; the cell itself has no view to ask.
                            required property bool selected
                            required property bool current

                            editable: root.editable
                            highlighted: selected || current
                            defaultFont: (root.pres.monospace ?? false) ? Fonts.fixed
                                                                        : Fonts.body2
                        }
                    }
                }
            }
        }

        RowLayout {
            spacing: Spacing.GapHXs
            visible: root.pres.allowAdding || root.pres.allowRemoving

            Button {
                text: qsTr("Add")
                visible: root.pres.allowAdding
                enabled: root.editable
                onClicked: root.sourceModel.insertRows(root.sourceModel.rowCount(), 1)
            }

            Button {
                text: qsTr("Remove")
                visible: root.pres.allowRemoving
                enabled: root.editable && root.selectedRows.length > 0
                // Backwards, so that removing one does not move the next.
                onClicked: {
                    const rows = root.selectedRows
                    for (let i = rows.length - 1; i >= 0; --i)
                        root.sourceModel.removeRows(rows[i], 1)
                }
            }

            Item { Layout.fillWidth: true }
        }
    }
}

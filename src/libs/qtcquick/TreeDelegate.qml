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

    // The aspect is told where the reader is without every page having to say
    // so: an aspect that does not care answers the base's no-op.
    onCurrentIndexChanged: root.aspect?.setCurrentIndex(root.currentIndex)

    // A row was chosen and acted on - the widget views these replace called it
    // "activated" and reached it by double click or by Return. The index is in
    // the aspect's own model, like currentIndex above.
    signal rowActivated(index: var)

    // What a row offers when it is asked. A popup is not in the item tree, so
    // this is the only way to reach it from outside.
    readonly property alias rowMenu: rowMenu

    // A page showing a tree usually wants to offer these, and they are the
    // view's business rather than the aspect's.
    function expandAll(): void { view.expandRecursively(-1, -1) }
    function collapseAll(): void { view.collapseRecursively() }

    // Puts the reader on a row. The index is in the aspect's own model, like
    // everything else here; an invalid one clears the selection, which is what
    // "no folder in particular" comes to.
    function setCurrentIndex(index: var): void {
        if (!index || !index.valid) {
            view.selectionModel.clearCurrentIndex()
            view.selectionModel.clearSelection()
            return
        }
        const shown = root.rows.mapFromSource(index)
        if (!shown.valid)
            return
        view.expandToIndex(shown)
        view.selectionModel.setCurrentIndex(shown, ItemSelectionModel.ClearAndSelect)
    }

    // Opens a branch and leaves the reader where they were.
    function expandIndex(index: var): void {
        if (!index || !index.valid)
            return
        const shown = root.rows.mapFromSource(index)
        if (shown.valid)
            view.expandToIndex(shown)
    }

    // A page that filters the tree on the user's behalf - "show me what else
    // uses this shortcut" - writes into the field rather than into the proxy,
    // so that what is filtered and what the field says stay the same thing.
    function setFilter(text: string): void { filter.text = text }

    // Which of the top-level branches are open, in the order they are shown.
    // A page that changes what the tree holds - hiding some rows and showing
    // others - reads this before the change and puts it back afterwards, the
    // way the widget views these replace kept the reader's place across a
    // change of filter.
    function branchState(): var {
        const state = []
        for (let row = 0; row < view.rows; ++row) {
            if (view.depth(row) === 0)
                state.push(view.isExpanded(row))
        }
        return state
    }

    // A branch the state says nothing about is opened: it was not there before
    // the change, so the reader has not closed it.
    function setBranchState(state: var): void {
        let branch = 0
        for (let row = 0; row < view.rows; ++row) {
            if (view.depth(row) !== 0)
                continue
            if (branch < state.length && !state[branch])
                view.collapse(row)
            else
                view.expand(row)
            ++branch
        }
    }

    // Whether the rows may be dragged into a different order or under a
    // different branch. The model does the move through its own
    // mimeData()/dropMimeData(); see AspectModels.moveRow().
    readonly property bool reorderable: root.pres.allowReordering ?? false

    // Where a row being dragged would land. Kept here rather than on a
    // delegate: the row it is over is not the row it started on.
    property var dragIndex: null
    property var dropParent: null
    property int dropRow: -1

    function dropAt(index: var, below: bool): void {
        if (!index || !index.valid) {
            root.dropParent = null
            root.dropRow = -1
            return
        }
        const source = root.rows.mapToSource(index)
        // Onto a branch is into it; onto a leaf is beside it.
        if (root.treeModel.hasChildren(source) || root.rows.rowCount(index) > 0) {
            root.dropParent = source
            root.dropRow = 0
        } else {
            root.dropParent = source.parent
            root.dropRow = source.row + (below ? 1 : 0)
        }
    }

    function finishDrag(): void {
        if (root.dragIndex && root.dropRow >= 0) {
            AspectModels.moveRow(root.treeModel, root.rows.mapToSource(root.dragIndex),
                                 root.dropParent, root.dropRow)
        }
        root.dragIndex = null
        root.dropParent = null
        root.dropRow = -1
    }

    Connections {
        target: root.aspect

        // An aspect can change what it wants drawn - which entries a row
        // offers - and says so with this. The binding above cannot see it: a
        // call to presentation() records no dependency on anything.
        function onControlConfigurationChanged(): void {
            root.pres = AspectModels.presentation(root.aspect)
        }

        // A dialog that opens on a tree puts the cursor in its filter field,
        // so that the reader can narrow it down without reaching for the
        // mouse. focus rather than forceActiveFocus(), the same as
        // StringDelegate: active focus needs the window to be the active one.
        function onControlFocusRequested(): void {
            filter.focus = true
        }

        // A tree that is refilled each time its dialog opens - the virtual
        // functions of the base classes of whatever the cursor was on - is
        // shown whole, because nothing about the last set of rows says
        // anything about this one.
        function onControlExpandRequested(): void {
            root.expandAll()
        }

        // And the aspect saying which row the reader should be on - the folder
        // picked in the box above the tree is the folder shown in it.
        function onControlIndexRequested(index: var): void {
            root.setCurrentIndex(index)
        }

        // And a branch it wants open without moving the reader into it.
        function onControlExpandIndexRequested(index: var): void {
            root.expandIndex(index)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true
    Layout.fillHeight: true

    FormLabel {
        text: root.labelText
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

            // Built only where there are column names to put in it, the same
            // as TableDelegate: a one-column tree has none, and the style's
            // own heading warns on a name the model does not have.
            Loader {
                active: AspectModels.namesItsColumns(root.treeModel)
                Layout.fillWidth: true

                sourceComponent: HorizontalHeaderView {
                    syncView: view
                    clip: true
                }
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

                // Return on the row the keyboard is on, and a double click:
                // the two halves of what a widget view called activation.
                Keys.onPressed: (event) => {
                    if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter)
                        return
                    const index = root.currentIndex
                    if (!index || !index.valid)
                        return
                    root.rowActivated(index)
                    event.accepted = true
                }

                TapHandler {
                    objectName: "treeActivateTap"
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.DragThreshold

                    onDoubleTapped: (eventPoint) => {
                        const cell = view.cellAtPosition(eventPoint.position, true)
                        if (cell.y < 0)
                            return
                        const index = root.rows.mapToSource(view.index(cell.y, cell.x))
                        if (index && index.valid)
                            root.rowActivated(index)
                    }
                }
                ToolTip.text: root.toolTip
                ToolTip.visible: false
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Every column as wide as its contents want, up to a cap, and
                // the last one takes what is left; the same rule TableDelegate
                // follows, and for the same reason - a column wider than the
                // view scrolls its header label out of sight.
                // A function literal is what TableView's own documentation assigns
                // here, and the property is a QJSValue. qmllint wants a type it has
                // no way to name: annotating the signature turns two complaints into
                // one about QJSValue itself.
                // qmllint disable Quick.unexpected-var-type
                columnWidthProvider: function (column) {
                    // What every column but the last wants. They get it when
                    // the table can hold it - the widget header sized each
                    // column to its contents and stretched the last - and are
                    // capped only when it cannot, which is the case the cap
                    // was written for.
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
                onWidthChanged: Qt.callLater(view.forceLayout)

                ScrollBar.vertical: ScrollBar {}

                // What a row offers when it is asked. A popup is not in the
                // item tree, so it is named here for a test to find, and it is
                // built only where the aspect describes something to offer.
                Menu {
                    id: rowMenu

                    objectName: "treeRowMenu"

                    Repeater {
                        model: root.pres.rowActions ?? []

                        delegate: MenuItem {
                            id: entry

                            required property var modelData

                            text: entry.modelData.display
                            enabled: entry.modelData.enabled
                            onTriggered: root.aspect?.triggerRowAction(root.currentIndex,
                                                                       entry.modelData.id)
                        }
                    }
                }

                delegate: TreeViewDelegate {
                    id: cell

                    // Only the column: TreeViewDelegate already requires row
                    // and model, and redeclaring one of those shadows the
                    // base's, which the view then never initialises - the
                    // delegate fails to incubate and no row is drawn at all.
                    required property int column

                    // The indent and the branch handle are the delegate's
                    // left padding, so a column sized from the content alone
                    // leaves the first column's text short by exactly them.
                    implicitWidth: Math.max(Metrics.lineEditWidth,
                                            implicitContentWidth + leftPadding
                                                + rightPadding)
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

                    // Asking a row what it offers. The row is made current
                    // first: what the menu does is about the row that was
                    // asked, not about wherever the reader happened to be.
                    TapHandler {
                        objectName: "treeRowMenuTap"
                        enabled: (root.pres.rowActions ?? []).length > 0
                        acceptedButtons: Qt.RightButton

                        onTapped: {
                            view.selectionModel.setCurrentIndex(
                                view.index(cell.row, 0), ItemSelectionModel.ClearAndSelect)
                            rowMenu.popup()
                        }
                    }

                    // Dragging a row somewhere else. Only where the tree says
                    // its order is the user's, and only from the first column
                    // - the row is dragged, not the cell.
                    DragHandler {
                        enabled: root.reorderable && root.editable && cell.column === 0
                        target: null
                        onActiveChanged: {
                            if (active)
                                root.dragIndex = view.index(cell.row, 0)
                            else
                                root.finishDrag()
                        }
                        onCentroidChanged: {
                            if (!active)
                                return
                            const point = cell.mapToItem(view, centroid.position.x,
                                                         centroid.position.y)
                            const over = view.cellAtPosition(point.x, point.y, true)
                            if (over.y < 0)
                                return
                            const overIndex = view.index(over.y, 0)
                            const cellY = view.mapToItem(view.contentItem, point.x, point.y).y
                            root.dropAt(overIndex,
                                        cellY % Metrics.tableRowMinimumHeight
                                            > Metrics.tableRowMinimumHeight / 2)
                        }
                    }
                }
            }
        }
    }
}

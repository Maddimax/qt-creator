// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// The Bookmarks sidebar, drawn in Qt Quick. What a row does is the
// controller's: see TextEditor::Internal::BookmarksList.
ListView {
    id: root

    // Handed over before the source is set, by whichever front end hosts this.
    required property var controller

    // The row a drag started on, for as long as it is in flight - what the
    // widget view keeps inside its item view. -1 when nothing is being
    // dragged, which is what makes a drag from anywhere else reorder nothing.
    property int draggedRow: -1

    objectName: "bookmarksList"
    model: root.controller.model
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    focus: true
    keyNavigationEnabled: true

    // The row is the manager's, not this list's: Previous and Next move that
    // selection from the toolbar above, so the list follows it and writes it
    // back. Writing it on every move is what keeps those two buttons enabled
    // for the row a reader walked to.
    //
    // Followed by assignment rather than by a binding: a binding whose own
    // handler writes to what it reads is a loop, and the reorder is where
    // that showed - a drop moves the manager's current row from under the
    // list. Each direction stops after one round trip, because
    // setCurrentRow() returns early on the row that is already current and
    // assigning the same currentIndex signals nothing.
    onCurrentIndexChanged: root.controller.currentRow = root.currentIndex

    Component.onCompleted: root.currentIndex = root.controller.currentRow

    Connections {
        target: root.controller

        function onCurrentRowChanged(): void {
            root.currentIndex = root.controller.currentRow
        }
    }

    // Always shown, as in the widget view: a bar that comes and goes moves
    // every row sideways when a bookmark is added.
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }

    Keys.onReturnPressed: (event) => {
        root.controller.activate(root.currentIndex)
        event.accepted = true
    }
    // Delete and Backspace remove the bookmark being looked at, unmodified
    // only - the widget view's keyPressEvent does the same.
    Keys.onPressed: (event) => {
        if (event.modifiers !== Qt.NoModifier)
            return
        if (event.key !== Qt.Key_Delete && event.key !== Qt.Key_Backspace)
            return
        if (root.currentIndex < 0)
            return
        root.controller.remove(root.currentIndex)
        event.accepted = true
    }

    // Which row a right-click landed on, made the current one on the way.
    // Move Up, Move Down and Edit act on the manager's current row rather
    // than on the row the menu was opened over; the widget view gets that
    // from the item view's own right-press, and this pane has to do it
    // itself. -1 where the click missed every row.
    function pickRowAt(x: real, y: real) : int {
        // indexAt() works in content coordinates, and a handler declared in a
        // Flickable is attached to the Flickable, so its points are not.
        const row = root.indexAt(x + root.contentX, y + root.contentY)
        if (row >= 0)
            root.controller.currentRow = row
        return row
    }

    // Where a dragged row was let go. The dragged row is the one the gesture
    // started on; the target is whatever is under the pointer, and -1 - past
    // the last row - is a drop at the end, which is what the model makes of a
    // drop that names no row.
    function dropRowAt(x: real, y: real) : bool {
        if (root.draggedRow < 0)
            return false
        return root.controller.dropRowOn(
            root.draggedRow, root.indexAt(x + root.contentX, y + root.contentY))
    }

    // The whole visible list takes a drop, not just its rows, so that letting
    // go below the last one means the end. An Item declared inside a
    // Flickable is reparented to its contentItem - a pointer handler is not -
    // so this is placed in content coordinates to sit over the viewport, and
    // the point it reports is relative to itself.
    DropArea {
        objectName: "bookmarkDropArea"
        x: root.contentX
        y: root.contentY
        width: root.width
        height: root.height

        onDropped: (drop) => {
            drop.accepted = root.dropRowAt(drop.x, drop.y)
        }
    }

    // The right-click menu, from the same six entries the widget view builds.
    // One handler for the whole list rather than one per row, because a
    // right-click that lands on no row is not "no menu": it is the menu with
    // the four entries about a row disabled, which is what a row of -1 says.
    // A row's ItemDelegate takes the left button only, so this sees the right
    // one wherever it falls.
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: (point, button) => {
            const row = root.pickRowAt(point.position.x, point.position.y)
            rowMenu.actions = root.controller.contextMenuActions(row)
            rowMenu.popup()
        }
    }

    Menu {
        id: rowMenu

        objectName: "bookmarkMenu"
        property var actions: []

        Repeater {
            model: rowMenu.actions

            delegate: MenuItem {
                required property var modelData

                // A separator arrives as an action with no text; the widget
                // menu draws a line there and so does this.
                text: modelData.text
                enabled: modelData.enabled && modelData.text !== ""
                height: modelData.text === "" ? 1 : implicitHeight
                onTriggered: modelData.trigger()
            }
        }
    }

    delegate: ItemDelegate {
        id: row

        // The roles the bookmark model names, and the row's own index. Not
        // "display": ItemDelegate has a FINAL property of that name.
        required property int index
        required property string filename
        required property int lineNumber
        required property string lineText
        required property string note

        objectName: "bookmarkRow"
        width: root.width
        height: Metrics.tableRowMinimumHeight * 2
        highlighted: root.currentIndex === row.index
        // A bookmark is two lines in the widget view as well: where it is,
        // and what is written there - the note if there is one, and the
        // line's own text otherwise.
        contentItem: Column {
            spacing: 0

            Label {
                width: parent.width
                text: row.filename + ":" + row.lineNumber
                elide: Text.ElideMiddle
            }

            Label {
                width: parent.width
                // Trimmed, as the widget delegate compares it: a note of
                // nothing but spaces is not a note, and the line's own text
                // says more.
                text: row.note.trim() !== "" ? row.note : row.lineText
                elide: Text.ElideRight
                opacity: 0.7
            }
        }

        // Opening a bookmark takes two clicks in the widget view, which is
        // deliberate there: one click is how a reader walks the list.
        onDoubleClicked: root.controller.activate(row.index)

        // Dragging a row carries its file and line away, and letting go
        // inside the list reorders. The drag itself is the controller's,
        // because what a drop target reads is the model's own mime data and
        // QML cannot build one; startDrag() holds until the reader lets go,
        // which is why the row being dragged is written before it and
        // forgotten after.
        DragHandler {
            objectName: "bookmarkDrag"
            target: null
            onActiveChanged: {
                if (!active)
                    return
                root.draggedRow = row.index
                root.controller.startDrag(row.index)
                root.draggedRow = -1
            }
        }
    }
}

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

    objectName: "bookmarksList"
    model: root.controller.model
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    focus: true
    keyNavigationEnabled: true

    // The row is the manager's, not this list's: Previous and Next move that
    // selection from the toolbar above, so the list reads it and writes it
    // rather than keeping one of its own. Writing it on every move is what
    // keeps those two buttons enabled for the row a reader walked to.
    currentIndex: root.controller.currentRow
    onCurrentIndexChanged: root.controller.currentRow = root.currentIndex

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
                text: row.note !== "" ? row.note : row.lineText
                elide: Text.ElideRight
                opacity: 0.7
            }
        }

        // Opening a bookmark takes two clicks in the widget view, which is
        // deliberate there: one click is how a reader walks the list.
        onDoubleClicked: root.controller.activate(row.index)
    }
}

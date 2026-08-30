// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// The debugger log's input side: the commands that were sent, which can be
// edited and sent again. Ctrl+Return runs the line the cursor is on, Ctrl+R
// clears, and double-clicking a line jumps the transcript beside it to that
// command's answer.
//
// The text belongs to the document rather than to this item: the pane writes
// into it, and a QSyntaxHighlighter colours the time stamps on it.
Item {
    id: root

    // The document the pane writes into and colours. Handed out rather than
    // filled from here, because what is in it is a transcript the pane keeps.
    readonly property alias editorDocument: area.textDocument
    readonly property alias cursorPosition: area.cursorPosition
    readonly property alias selectionStart: area.selectionStart
    readonly property alias selectionEnd: area.selectionEnd

    signal executeLineRequested()
    signal clearRequested()
    signal doubleClickedAt(position: int)
    signal focusMoved(focused: bool)

    function select(anchor: int, position: int): void {
        area.select(anchor, position)
    }

    function moveCursorTo(position: int): void {
        area.cursorPosition = position
    }

    ScrollView {
        anchors.fill: parent
        clip: true

        TextArea {
            id: area

            objectName: "input"
            wrapMode: TextEdit.NoWrap
            selectByMouse: true
            font: Fonts.body1

            onActiveFocusChanged: root.focusMoved(area.activeFocus)

            Keys.onPressed: (event) => {
                if (event.modifiers !== Qt.ControlModifier)
                    return
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.executeLineRequested()
                    event.accepted = true
                } else if (event.key === Qt.Key_R) {
                    root.clearRequested()
                    event.accepted = true
                }
            }

            TapHandler {
                onDoubleTapped: (eventPoint) => {
                    root.doubleClickedAt(area.positionAt(eventPoint.position.x,
                                                         eventPoint.position.y))
                }
            }
        }
    }
}

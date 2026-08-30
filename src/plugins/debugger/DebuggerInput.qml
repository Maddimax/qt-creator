// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The debugger log's input side: the commands that were sent, which can be
// edited and sent again, and a box for typing one straight in.
//
// Ctrl+Return runs the line the cursor is on, Ctrl+R clears, and
// double-clicking a line jumps the transcript beside it to that command's
// answer.
//
// The text belongs to the document rather than to this item: the pane writes
// into it, and a QSyntaxHighlighter colours the time stamps on it.
Item {
    id: root

    required property var pane

    // The document the pane writes into and colours. Handed out rather than
    // filled from here, because what is in it is a transcript the pane keeps.
    readonly property alias editorDocument: area.textDocument
    readonly property alias cursorPosition: area.cursorPosition
    readonly property alias selectionStart: area.selectionStart
    readonly property alias selectionEnd: area.selectionEnd
    readonly property alias commandText: command.text

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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
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

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 2
            spacing: 6

            QtcIconDisplay {
                objectName: "repeatButton"

                iconSource: AspectModels.decorationUrl(root.pane.repeatIcon)
                implicitWidth: 18
                implicitHeight: 18
                ToolTip.text: qsTr("Repeat last command for debug reasons.")
                ToolTip.visible: repeatHover.hovered

                HoverHandler { id: repeatHover }
                TapHandler { onTapped: root.pane.repeatLastCommand() }
            }

            QtcLabel { text: qsTr("Command:") }

            TextField {
                id: command

                objectName: "commandField"

                Layout.fillWidth: true
                font: Fonts.body1

                // What was typed here before, offered back. The store is the
                // one the widget field used, so a log that already has a
                // history keeps it.
                onTextEdited: completion.offer()

                onAccepted: {
                    root.pane.rememberCommand(command.text)
                    root.pane.sendCommand(command.text)
                }

                // Focus-out records too, as the widget field did: what was
                // typed is worth remembering even when it was not run from
                // here.
                onActiveFocusChanged: {
                    if (!command.activeFocus)
                        root.pane.rememberCommand(command.text)
                }

                Keys.onPressed: (event) => {
                    if (!completion.visible)
                        return
                    switch (event.key) {
                    case Qt.Key_Down: completion.moveDown(); event.accepted = true; break
                    case Qt.Key_Up: completion.moveUp(); event.accepted = true; break
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                    case Qt.Key_Tab: completion.acceptCurrent(); event.accepted = true; break
                    }
                }

                CompletionPopup {
                    id: completion

                    objectName: "commandHistory"

                    completions: root.pane.commandHistory
                    prefix: command.text
                    x: command.cursorRectangle.x
                    y: command.cursorRectangle.y + command.cursorRectangle.height

                    onAccepted: (text) => {
                        command.text = text
                        command.cursorPosition = text.length
                    }
                }
            }
        }
    }
}

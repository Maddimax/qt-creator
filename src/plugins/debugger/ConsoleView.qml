// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// The debugger console. Each row is a message or an answer: an icon saying
// which kind it is, what it said, and the file and line it came from.
//
// A row shows one line until it is the current one, and then it wraps to show
// everything - and gives up the file and line to do it, because the message is
// what the reader asked for.
//
// The last row is the prompt, and it is a row rather than a bar under the list
// because that is where the model keeps it: what is typed is written into it,
// and running it turns it into history with a fresh prompt underneath.
Item {
    id: root

    property alias consoleRows: view.model
    property var selection: null
    required property var pane

    signal rowActivated(index: var)
    signal contextMenuRequested(index: var)

    // Bumped rather than called, because the prompt is a delegate: the object
    // that has to take focus does not exist until its row is on screen.
    property int focusRequest: 0

    function focusPrompt(): void {
        root.focusRequest++
    }

    function scrollToBottom(): void {
        if (view.rows > 0)
            view.positionViewAtRow(view.rows - 1, Qt.AlignBottom)
    }

    TreeView {
        id: view
        objectName: "consoleRows"

        anchors.fill: parent
        clip: true
        selectionModel: root.selection

        delegate: TreeViewDelegate {
            id: row

            required property int column

            implicitWidth: view.width
            implicitHeight: content.implicitHeight + 6

            readonly property bool isPrompt: row.model.editable ?? false

            contentItem: Item {
                id: content

                implicitHeight: Math.max(16, row.isPrompt ? input.implicitHeight
                                                          : said.implicitHeight)

                // Only a top-level row says which kind of message it is: the
                // rows under one are the parts of its answer, not messages of
                // their own.
                Image {
                    id: kind
                    anchors.left: parent.left
                    anchors.top: parent.top
                    width: 16
                    height: 16
                    fillMode: Image.PreserveAspectFit
                    visible: row.depth === 0
                    source: AspectModels.decorationUrl(row.model.decoration)
                }

                Text {
                    id: said
                    objectName: "said"
                    anchors.left: kind.visible ? kind.right : parent.left
                    anchors.leftMargin: kind.visible ? 6 : 0
                    anchors.right: where.visible ? where.left : parent.right
                    anchors.rightMargin: 6
                    anchors.top: parent.top
                    visible: !row.isPrompt
                    text: row.model.display ?? ""
                    color: row.model.textColor ?? Tokens.foregroundDefault
                    elide: row.current ? Text.ElideNone : Text.ElideRight
                    wrapMode: row.current ? Text.WordWrap : Text.NoWrap
                    font: Fonts.body1
                }

                // What is being typed. Enter runs it; Up and Down walk what was
                // run before, which only the rows know, so the pane is asked.
                TextInput {
                    id: input
                    objectName: "prompt"
                    anchors.left: kind.visible ? kind.right : parent.left
                    anchors.leftMargin: kind.visible ? 6 : 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    visible: row.isPrompt
                    enabled: row.isPrompt
                    color: row.model.textColor ?? Tokens.foregroundDefault
                    font: Fonts.body1
                    selectByMouse: true

                    // The delegate outlives the row it was made for, so the
                    // text is re-set when the row changes rather than bound:
                    // walking the history assigns to it, and an assignment
                    // would break a binding for good.
                    readonly property string rowText: row.model.expression ?? ""
                    onRowTextChanged: input.text = input.rowText
                    Component.onCompleted: {
                        input.text = input.rowText
                        if (row.isPrompt)
                            input.forceActiveFocus()
                    }

                    Connections {
                        target: root
                        function onFocusRequestChanged(): void {
                            if (row.isPrompt)
                                input.forceActiveFocus()
                        }
                    }

                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            root.pane.evaluate(input.text)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up) {
                            input.text = root.pane.historyUp(input.text)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Down) {
                            input.text = root.pane.historyDown(input.text)
                            event.accepted = true
                        }
                    }
                }

                Row {
                    id: where
                    objectName: "where"
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: 4
                    // Nothing to say where it came from, nothing shown.
                    visible: !row.isPrompt && (row.model.fileName ?? "") !== ""

                    Text {
                        objectName: "fileName"
                        text: row.model.fileName ?? ""
                        color: Tokens.foregroundSubtle
                        font: Fonts.body1
                    }
                    Text {
                        objectName: "line"
                        text: (row.model.line ?? -1) > 0 ? String(row.model.line) : ""
                        color: Tokens.foregroundSubtle
                        font: Fonts.body1
                    }
                }
            }

            onDoubleClicked: root.rowActivated(view.index(row.row, 0))

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    root.contextMenuRequested(view.index(row.row, 0))
                    menu.popup()
                }
            }
        }

        // Copy, Show in Editor and Clear, which the pane enables against the
        // row the menu was opened over before it is shown.
        Menu {
            id: menu

            objectName: "consoleContextMenu"

            Repeater {
                model: root.pane.contextActions

                delegate: MenuItem {
                    id: entry

                    required property int index
                    required property string actionText
                    required property bool actionEnabled
                    required property bool actionSeparator

                    text: entry.actionSeparator ? "" : entry.actionText
                    enabled: !entry.actionSeparator && entry.actionEnabled
                    onTriggered: root.pane.contextActions.trigger(entry.index)
                }
            }
        }
    }
}

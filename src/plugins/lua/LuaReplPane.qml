// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// The Lua REPL: what it has said, and a line to tell it something. What a line
// says and what submitting one does are the controller's: see
// Lua::Internal::LuaReplController.
Item {
    id: root

    // Handed over before the source is set, by whichever front end hosts this.
    required property var controller

    objectName: "luaReplPane"

    Component.onCompleted: root.controller.start()

    ListView {
        id: lines

        objectName: "luaReplLines"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: inputRow.top
        clip: true
        model: root.controller.model
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {}

        delegate: Label {
            required property var model

            objectName: "luaReplLine"
            width: ListView.view.width
            text: model.display
            // The one thing this pane draws differently from plain output, and
            // a role rather than a marker in the text since the model has one.
            color: model.isError ? Tokens.notificationDangerDefault : Tokens.textDefault
            font: Fonts.fixed
            wrapMode: Text.WrapAnywhere
        }

        Connections {
            target: root.controller

            // A line arriving is the cue to follow it down, which the list
            // cannot work out for itself: a reader who has scrolled up is
            // reading, and positionViewAtEnd would take that away.
            function onLinePrinted(): void {
                if (lines.atYEnd)
                    lines.positionViewAtEnd()
            }
        }
    }

    Row {
        id: inputRow

        objectName: "luaReplInputRow"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: Spacing.GapHXs
        padding: Spacing.PaddingHXs

        Label {
            objectName: "luaReplPrompt"
            text: root.controller.prompt
            color: Tokens.textMuted
            font: Fonts.fixed
            anchors.verticalCenter: parent.verticalCenter
        }

        TextField {
            id: input

            // How far back through the history the reader has walked. -1 is
            // the line being typed, which is why walking back down past 0
            // restores it rather than leaving the last recalled entry there.
            property int recalled: -1
            property string typed: ""

            objectName: "luaReplInput"
            width: inputRow.width - inputRow.padding * 2 - parent.spacing
                   - inputRow.children[0].width
            font: Fonts.fixed
            // Empty while an answer is being worked out: the REPL says what it
            // is waiting with, and nothing to show means it is not waiting.
            enabled: root.controller.prompt !== ""

            onAccepted: {
                root.controller.submit(input.text)
                input.text = ""
                input.recalled = -1
                input.typed = ""
            }

            Keys.onUpPressed: input.walkHistory(1)
            Keys.onDownPressed: input.walkHistory(-1)

            // The same entries the widget line edit completes from, walked the
            // way a terminal walks them rather than offered as a drop-down.
            function walkHistory(by: int): void {
                const history = root.controller.history
                const count = history.rowCount()
                if (count === 0)
                    return
                if (input.recalled === -1 && by > 0)
                    input.typed = input.text
                const next = Math.max(-1, Math.min(count - 1, input.recalled + by))
                if (next === input.recalled)
                    return
                input.recalled = next
                input.text = next === -1
                           ? input.typed
                           : history.data(history.index(next, 0), Qt.DisplayRole)
            }
        }
    }
}

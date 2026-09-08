// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// The Open Documents sidebar, drawn in Qt Quick. What a row *does* is the
// controller's, not this file's: see Core::Internal::OpenDocumentsList.
ListView {
    id: root

    // Handed over before the source is set, by whichever front end hosts this.
    required property var controller

    objectName: "openDocumentsList"
    model: root.controller.model
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    focus: true
    // Arrows walk the list on their own, which is the half of this that
    // must not open anything: the tree view lets a reader look down the
    // documents and only Return or a click takes them there.
    keyNavigationEnabled: true

    // currentIndex is written, not bound: ListView writes it itself while the
    // arrows walk, and a binding to the editor manager's answer would be two
    // things fighting over one property. It is set when the list is built and
    // again whenever the reader leaves - the snap-back the tree view does
    // from focusChanged, so a walk that opened nothing does not leave the
    // sidebar pointing at a document nobody is in.
    //
    // Nothing listens for currentRowChanged, and a control is why: every path
    // that changes the current editor activates it, activation takes the
    // keyboard, and losing the keyboard is what runs the snap-back below. A
    // listener as well was code no test could tell apart from its absence.
    Component.onCompleted: root.currentIndex = root.controller.currentRow
    onActiveFocusChanged: {
        if (!root.activeFocus)
            root.currentIndex = root.controller.currentRow
    }

    Keys.onReturnPressed: (event) => {
        root.controller.activate(root.currentIndex)
        event.accepted = true
    }
    // Delete and Backspace close the row being looked at, which is what the
    // tree view's event filter does - and neither takes a modifier, so a
    // chord that happens to use them is left alone.
    Keys.onPressed: (event) => {
        if (event.modifiers !== Qt.NoModifier)
            return
        if (event.key !== Qt.Key_Delete && event.key !== Qt.Key_Backspace)
            return
        if (root.currentIndex < 0)
            return
        root.controller.close(root.currentIndex)
        event.accepted = true
    }
    Keys.onEnterPressed: (event) => {
        root.controller.activate(root.currentIndex)
        event.accepted = true
    }

    ScrollBar.vertical: ScrollBar {}

    delegate: ItemDelegate {
        id: row

        // The row's own index and the model row behind it. Not "display" as
        // a property of its own: ItemDelegate has a FINAL one of that name -
        // for where it puts an icon - and overriding it fails to load the
        // whole file.
        required property int index
        required property var model

        objectName: "openDocumentRow"
        width: root.width
        height: Metrics.tableRowMinimumHeight
        highlighted: root.currentIndex === row.index
        text: row.model.display
        // The icon, the colour and the tool tip are the model's answers, and
        // the model is the one the widget sidebar uses: the colour says what
        // version control makes of the file, the tip names that state.
        icon.source: AspectModels.decorationUrl(row.model.decoration)
        icon.color: "transparent"
        ToolTip.text: row.model.toolTip
        ToolTip.visible: row.hovered && row.model.toolTip !== ""
        ToolTip.delay: 800
        // The reader's own click opens a document, as in the widget view;
        // nothing here opens anything by itself.
        onClicked: root.controller.activate(row.index)

        // The middle button closes the row under the pointer, as it does in
        // the tree view. Unmodified only, for the same reason as the keys.
        TapHandler {
            acceptedButtons: Qt.MiddleButton
            onTapped: (point, button) => {
                if (point.modifiers === Qt.NoModifier)
                    root.controller.close(row.index)
            }
        }

        // Dragging a row takes the document with it, the way the tree view
        // does - the drag itself is the controller's, because what a drop
        // target reads is the model's own mime data and QML cannot build it.
        DragHandler {
            objectName: "openDocumentDrag"
            target: null
            onActiveChanged: {
                if (active)
                    root.controller.startDrag(row.index)
            }
        }

        // The right-click menu the widget sidebar offers, from the same
        // actions. Built when it is asked for rather than kept: the entries
        // are about this row and go stale as soon as another is clicked.
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                rowMenu.actions = root.controller.contextMenuActions(row.index)
                rowMenu.popup()
            }
        }

        Menu {
            id: rowMenu

            objectName: "openDocumentMenu"
            property var actions: []

            Repeater {
                model: rowMenu.actions

                delegate: MenuItem {
                    required property var modelData

                    // A separator arrives as an action with no text; the
                    // widget menu draws a line there and so does this.
                    text: modelData.text
                    enabled: modelData.enabled && modelData.text !== ""
                    height: modelData.text === "" ? 1 : implicitHeight
                    onTriggered: modelData.trigger()
                }
            }
        }

        // The label is drawn here rather than left to ItemDelegate, because
        // the colour is the model's and contentItem is where it reaches.
        contentItem: Label {
            text: row.text
            color: row.model.foreground !== undefined ? row.model.foreground
                                                      : row.palette.text
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
            leftPadding: row.icon.source !== "" ? Spacing.GapHM : 0
        }

        ToolButton {
            objectName: "closeDocument"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Metrics.tableRowMinimumHeight
            // Where the widget view shows its close button: on the row the
            // pointer is over, and on the one being read.
            visible: row.hovered || row.highlighted
            icon.source: "image://qtcreator/utils/images/close.png?color=IconsBaseColor"
            onClicked: root.controller.close(row.index)
        }
    }
}

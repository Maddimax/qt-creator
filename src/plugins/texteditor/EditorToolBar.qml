// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui
import QtCreator.TextEditor

// What an editor puts in the toolbar row the editor manager draws above it.
// Core supplies the file list and the close and split buttons; this is the
// part that is the editor's own, and for now that is where the caret is.
Row {
    id: root

    // The form this belongs to is a separate component from the editor's, so
    // the viewport is handed over rather than found.
    required property TextViewport viewport

    spacing: Spacing.GapHM

    QtcLabel {
        objectName: "lineColumnLabel"

        anchors.verticalCenter: parent.verticalCenter
        // The same wording the widget editor uses, so the two read alike.
        text: {
            const where = qsTr("Line: %1, Col: %2")
                              .arg(root.viewport.cursorLine)
                              .arg(root.viewport.cursorDisplayColumn)
            const selected = root.viewport.selectedCharacterCount
            return selected > 0 ? where + " " + qsTr("(Sel: %1)").arg(selected) : where
        }
    }

    // What the file is, beside where the caret is. Both empty themselves when
    // the display settings say not to show them, so there is no second copy of
    // that rule here.
    QtcLabel {
        id: lineEnding

        objectName: "lineEndingLabel"

        anchors.verticalCenter: parent.verticalCenter
        text: root.viewport.fileLineEnding
        visible: text !== ""

        TapHandler {
            onTapped: lineEndingMenu.popup()
        }

        Menu {
            id: lineEndingMenu

            objectName: "lineEndingMenu"

            MenuItem {
                objectName: "unixLineEndings"
                text: qsTr("Unix Line Endings (LF)")
                onTriggered: root.viewport.setFileLineEndingIsWindows(false)
            }
            MenuItem {
                objectName: "windowsLineEndings"
                text: qsTr("Windows Line Endings (CRLF)")
                onTriggered: root.viewport.setFileLineEndingIsWindows(true)
            }
        }
    }

    QtcLabel {
        objectName: "encodingLabel"

        anchors.verticalCenter: parent.verticalCenter
        text: root.viewport.fileEncoding
        visible: text !== ""
    }
}

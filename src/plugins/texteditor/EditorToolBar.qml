// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
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
}

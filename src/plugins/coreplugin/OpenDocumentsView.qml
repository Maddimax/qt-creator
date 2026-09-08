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
    // Which row the reader is in is the editor manager's answer, not a
    // selection this list keeps for itself.
    currentIndex: root.controller.currentRow

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
        // The reader's own click opens a document, as in the widget view;
        // nothing here opens anything by itself.
        onClicked: root.controller.activate(row.index)

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

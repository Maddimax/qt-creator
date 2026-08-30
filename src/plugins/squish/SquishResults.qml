// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// The Squish test results. Three columns - the result type, the message and
// the time - with no header: the widget set one and then hid it.
Item {
    id: root

    required property var pane

    // Shown only once there are results, as the widget did: it set a header
    // and kept it hidden until the first one arrived.
    HorizontalHeaderView {
        id: header
        objectName: "squishResultsHeader"

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        syncView: view
        clip: true
        visible: view.rows > 0
    }

    TreeView {
        id: view
        objectName: "squishResults"

        anchors.top: header.visible ? header.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        model: root.pane.rows
        selectionModel: root.pane.selection

        // row, column and model come from TreeViewDelegate. Redeclaring them
        // shadows the base type's, and then nothing is drawn at all.
        delegate: TreeViewDelegate {
            id: cell

            // row and model come from TreeViewDelegate; column does not, so
            // it is the one that has to be asked for.
            required property int column

            implicitWidth: cell.column === 1 ? Math.max(240, root.width / 2) : 120
            // The colour the model asks for, where it asks for one: the result
            // type is read in the colour of its severity.
            contentItem: Text {
                text: cell.model.display ?? ""
                color: cell.model.foreground ?? Tokens.foregroundDefault
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            ToolTip.visible: cell.hovered && (cell.model.cellToolTip ?? "") !== ""
            ToolTip.text: cell.model.cellToolTip ?? ""

            onDoubleClicked: root.pane.activate(view.index(cell.row, 0))
        }

        Connections {
            target: root.pane
            function onExpandAllRequested(): void { view.expandRecursively() }
            function onCollapseAllRequested(): void { view.collapseRecursively() }
            function onExpandRequested(index: var): void { view.expandToIndex(index) }
        }
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The issues a build reported: an icon, what went wrong, and where. A task
// that has more to say carries it in a row of its own underneath, which is
// where the links are.
//
// Which row is current, which are selected and what the menu offers all belong
// to the pane - a view that kept them was why nothing could ask the pane
// anything without drawing one.
TreeView {
    id: root

    // ProjectExplorer::Internal::TaskWindow.
    required property var pane

    objectName: "issuesList"
    model: root.pane.rows
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    selectionBehavior: TableView.SelectRows
    selectionMode: TableView.ExtendedSelection
    selectionModel: ItemSelectionModel { model: root.model }

    ScrollBar.vertical: ScrollBar {}

    // Every task the walk lands on is scrolled to, which the view used to do
    // for itself when its current index changed.
    Connections {
        target: root.pane

        function onCurrentRowChanged(): void {
            if (root.pane.currentRow >= 0)
                root.positionViewAtRow(root.pane.currentRow, TableView.Contain)
        }
    }

    // What the handlers act on. The pane is told, rather than asked, because a
    // Quick selection model is the view's and the handlers outlive the view.
    Connections {
        target: root.selectionModel

        function onSelectionChanged(): void {
            const rows = []
            const indexes = root.selectionModel.selectedIndexes
            for (let i = 0; i < indexes.length; ++i) {
                const index = indexes[i]
                if (!index.parent.valid && rows.indexOf(index.row) === -1)
                    rows.push(index.row)
            }
            root.pane.setSelectedRows(rows)
        }
    }

    delegate: TreeViewDelegate {
        id: task

        // Only the column: TreeViewDelegate already requires row, model and
        // text, and redeclaring one of those shadows the base's - which the
        // view then never initialises, so no row is drawn at all. "display"
        // shadows AbstractButton's own property of that name.
        required property int column

        // A task's own row is two columns - what went wrong and where - and
        // the row underneath it is the description, which is one.
        readonly property bool isDetail: task.column === 0 && task.depth > 0

        objectName: "issueRow"
        implicitWidth: Math.max(Metrics.lineEditWidth,
                                implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: Math.max(Metrics.tableRowMinimumHeight, implicitContentHeight)
        indentation: task.isDetail ? Metrics.listRowIconSize : 0

        // The stock delegate draws a label; a task draws an icon beside one,
        // and a description draws marked-up text with links in it.
        contentItem: RowLayout {
            spacing: Spacing.GapHXs

            Image {
                source: AspectModels.decorationUrl(task.model.decoration ?? undefined)
                visible: source.toString() !== ""
                sourceSize.width: Metrics.listRowIconSize
                sourceSize.height: Metrics.listRowIconSize
                Layout.preferredWidth: visible ? Metrics.listRowIconSize : 0
                Layout.preferredHeight: Metrics.listRowIconSize
            }

            Label {
                text: task.text
                // The description arrives as marked-up text with links in it;
                // a summary is plain and is elided in the middle, which is
                // where a path has least to say.
                textFormat: task.isDetail ? Text.RichText : Text.PlainText
                elide: task.isDetail ? Text.ElideNone : Text.ElideMiddle
                wrapMode: task.isDetail ? Text.WordWrap : Text.NoWrap
                Layout.fillWidth: true

                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }

        onClicked: root.pane.setCurrentRow(task.row)
        onDoubleClicked: root.pane.activateRow(task.row)

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                root.pane.setCurrentRow(task.row)
                root.pane.contextActions.refresh()
                menu.popup()
            }
        }

        // Qt Creator's menus are QActions assembled by whatever registered a
        // task handler, so this lists what they produced rather than naming
        // any of it.
        Menu {
            id: menu

            objectName: "issuesContextMenu"

            Repeater {
                model: root.pane.contextActions

                delegate: MenuItem {
                    id: entry

                    required property int index
                    required property string actionText
                    required property bool actionEnabled
                    required property bool actionVisible
                    required property bool actionSeparator

                    text: entry.actionSeparator ? "" : entry.actionText
                    enabled: !entry.actionSeparator && entry.actionEnabled
                    visible: entry.actionVisible
                    onTriggered: root.pane.contextActions.trigger(entry.index)
                }
            }
        }
    }
}

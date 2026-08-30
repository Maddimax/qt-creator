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
Item {
    id: root

    property alias consoleRows: view.model
    property var selection: null

    signal rowActivated(index: var)

    TreeView {
        id: view
        objectName: "consoleRows"

        anchors.fill: parent
        clip: true
        selectionModel: root.selection

        delegate: TreeViewDelegate {
            id: row

            implicitWidth: view.width
            implicitHeight: content.implicitHeight + 6

            contentItem: Item {
                id: content

                implicitHeight: Math.max(16, said.implicitHeight)

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
                    text: row.model.display ?? ""
                    color: row.model.textColor ?? Tokens.foregroundDefault
                    elide: row.current ? Text.ElideNone : Text.ElideRight
                    wrapMode: row.current ? Text.WordWrap : Text.NoWrap
                    font: Fonts.body1
                }

                Row {
                    id: where
                    objectName: "where"
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: 4
                    // Nothing to say where it came from, nothing shown.
                    visible: (row.model.fileName ?? "") !== ""

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
        }
    }
}

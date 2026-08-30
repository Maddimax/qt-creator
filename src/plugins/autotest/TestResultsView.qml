// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// A test run's results. Each row is an icon, the outcome in the colour of its
// severity, what the test said, and where it said it.
//
// A row shows one line until it is the current one, and then it wraps and
// grows to show everything - which is what the widget delegate spent most of
// its 287 lines doing by hand.
Item {
    id: root

    property alias resultRows: view.model
    property var selection: null
    property bool showDuration: false

    signal rowActivated(index: var)
    signal contextMenuRequested(index: var)

    TreeView {
        id: view
        objectName: "testResults"

        anchors.fill: parent
        clip: true
        selectionModel: root.selection

        delegate: TreeViewDelegate {
            id: row

            implicitWidth: view.width
            implicitHeight: content.implicitHeight + 8

            contentItem: Item {
                id: content

                implicitHeight: Math.max(16, output.implicitHeight)

                Image {
                    id: icon
                    anchors.left: parent.left
                    anchors.top: parent.top
                    source: AspectModels.decorationUrl(row.model.decoration)
                    width: 16
                    height: 16
                    fillMode: Image.PreserveAspectFit
                }

                Text {
                    id: outcome
                    objectName: "resultString"
                    anchors.left: icon.right
                    anchors.leftMargin: 6
                    anchors.top: parent.top
                    text: row.model.resultString ?? ""
                    color: row.model.resultColor ?? Tokens.foregroundDefault
                    font: Fonts.body1
                }

                // Anchored between what is beside it rather than sized inside
                // a Row: a Row positions its children by their widths, so a
                // width worked out from x never settles and the text never
                // wraps.
                Text {
                    id: output
                    objectName: "output"
                    anchors.left: outcome.right
                    anchors.leftMargin: 6
                    anchors.right: duration.visible ? duration.left : whereRow.left
                    anchors.rightMargin: 6
                    anchors.top: parent.top
                    text: row.current ? (row.model.fullOutput ?? "")
                                      : (row.model.summary ?? "")
                    elide: row.current ? Text.ElideNone : Text.ElideRight
                    wrapMode: row.current ? Text.WordWrap : Text.NoWrap
                    font: Fonts.body1
                }

                Text {
                    id: duration
                    objectName: "duration"
                    anchors.right: whereRow.left
                    anchors.rightMargin: 6
                    anchors.top: parent.top
                    visible: root.showDuration && (row.model.duration ?? "") !== ""
                    text: (row.model.duration ?? "") + " ms"
                    color: Tokens.foregroundSubtle
                    font: Fonts.body1
                }

                Row {
                    id: whereRow
                    objectName: "where"
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: 4

                    Text {
                        objectName: "fileName"
                        text: row.model.fileName ?? ""
                        color: Tokens.foregroundSubtle
                        font: Fonts.body1
                    }
                    Text {
                        objectName: "line"
                        text: (row.model.line ?? 0) > 0 ? String(row.model.line) : ""
                        color: Tokens.foregroundSubtle
                        font: Fonts.body1
                    }
                }
            }

            onDoubleClicked: root.rowActivated(view.index(row.row, 0))
        }
    }
}

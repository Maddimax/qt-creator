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

    // What the document indents with, and how to change it for this document
    // only. Empties itself when the display settings say not to show it, the
    // same way the line ending beside it does.
    QtcLabel {
        id: tabSettings

        objectName: "tabSettingsLabel"

        anchors.verticalCenter: parent.verticalCenter
        text: root.viewport.tabSettingsLabel
        visible: text !== ""

        TapHandler {
            onTapped: tabSettingsMenu.popup()
        }

        Menu {
            id: tabSettingsMenu

            objectName: "tabSettingsMenu"

            MenuItem {
                objectName: "autoDetectTabSettings"
                text: qsTr("Auto Detect")
                onTriggered: root.viewport.detectTabSettings()
            }

            Menu {
                title: qsTr("Tab Policy")

                MenuItem {
                    objectName: "indentWithSpaces"
                    text: qsTr("Spaces")
                    onTriggered: root.viewport.setTabPolicyIsSpaces(true)
                }
                MenuItem {
                    objectName: "indentWithTabs"
                    text: qsTr("Tabs")
                    onTriggered: root.viewport.setTabPolicyIsSpaces(false)
                }
            }

            // Written out rather than repeated over a model: a Repeater's
            // delegates inside a Menu are not reachable from it, which makes
            // them untestable as well as lazily built.
            Menu {
                id: indentSizeMenu

                objectName: "indentSizeMenu"
                title: qsTr("Indent Size")

                MenuItem {
                    objectName: "indentSize1"
                    text: "1"
                    checkable: true
                    checked: root.viewport.indentSize === 1
                    onTriggered: root.viewport.setIndentSize(1)
                }
                MenuItem {
                    objectName: "indentSize2"
                    text: "2"
                    checkable: true
                    checked: root.viewport.indentSize === 2
                    onTriggered: root.viewport.setIndentSize(2)
                }
                MenuItem {
                    objectName: "indentSize3"
                    text: "3"
                    checkable: true
                    checked: root.viewport.indentSize === 3
                    onTriggered: root.viewport.setIndentSize(3)
                }
                MenuItem {
                    objectName: "indentSize4"
                    text: "4"
                    checkable: true
                    checked: root.viewport.indentSize === 4
                    onTriggered: root.viewport.setIndentSize(4)
                }
                MenuItem {
                    objectName: "indentSize5"
                    text: "5"
                    checkable: true
                    checked: root.viewport.indentSize === 5
                    onTriggered: root.viewport.setIndentSize(5)
                }
                MenuItem {
                    objectName: "indentSize6"
                    text: "6"
                    checkable: true
                    checked: root.viewport.indentSize === 6
                    onTriggered: root.viewport.setIndentSize(6)
                }
                MenuItem {
                    objectName: "indentSize7"
                    text: "7"
                    checkable: true
                    checked: root.viewport.indentSize === 7
                    onTriggered: root.viewport.setIndentSize(7)
                }
                MenuItem {
                    objectName: "indentSize8"
                    text: "8"
                    checkable: true
                    checked: root.viewport.indentSize === 8
                    onTriggered: root.viewport.setIndentSize(8)
                }
            }
        }
    }

    QtcLabel {
        objectName: "encodingLabel"

        anchors.verticalCenter: parent.verticalCenter
        text: root.viewport.fileEncoding
        visible: text !== ""

        TapHandler {
            onTapped: root.viewport.selectEncoding()
        }
    }
}

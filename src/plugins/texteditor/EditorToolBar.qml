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
    // What the language wants here - the preprocessor button for a C++ file.
    // A model of actions rather than widgets, so this decides how they look.
    required property ActionModel languageActions
    // Where the caret is in the file, as the language sees it: a tree of what
    // is in it and which row the caret is inside. Null where the language
    // keeps none, which is every language but C++ today.
    // Typed as var: ToolBarOutline is a plain QObject rather than a QML type,
    // and QML reaches its properties through the meta-object either way.
    required property var outline
    // The choices the document offers: which of several ways a C++ file is
    // parsed, which section of a log the caret is in, what a VCS command is
    // told. A list of ToolBarChoice objects, empty for most languages; the
    // editor sets it again when the document's list changes.
    required property var choices

    spacing: Spacing.GapHM

    // One combo box per choice, hidden while the choice has nothing to offer -
    // one project part is no choice, which is what the widget editor does
    // with it too.
    Repeater {
        model: root.choices

        delegate: Row {
            id: choiceRow

            required property var modelData
            readonly property var choice: choiceRow.modelData

            // Guarded: a Repeater's delegate has no parent while it is being
            // created, and an unguarded binding warns on every one of them.
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            spacing: Spacing.GapHM
            visible: choiceRow.choice.available

            ComboBox {
                id: choiceCombo

                objectName: "parseContextCombo"
                anchors.verticalCenter: parent.verticalCenter
                model: choiceRow.choice.model
                // Set rather than bound: a ComboBox writes its own currentIndex
                // when the model changes, so a binding would be broken by the
                // first refill. See the same trap with editText in the
                // migration notes.
                onModelChanged: choiceCombo.currentIndex = choiceRow.choice.currentIndex
                ToolTip.text: choiceRow.choice.toolTip
                ToolTip.visible: hovered && ToolTip.text !== ""
                onActivated: (index) => choiceRow.choice.choose(index)

                Connections {
                    target: choiceRow.choice
                    function onChanged(): void {
                        choiceCombo.currentIndex = choiceRow.choice.currentIndex
                    }
                }
            }

            // Undoing a pick, which only the reader can do - left alone the
            // language goes on choosing the same part. Shown only while there
            // is a pick, so its being there is also what says the current row
            // is one rather than what the language worked out.
            QtcButton {
                objectName: "clearParseContextButton"

                anchors.verticalCenter: parent.verticalCenter
                visible: choiceRow.choice.chosen
                role: QtcButton.Role.SmallList
                text: qsTr("Clear")
                ToolTip.text: qsTr("Parse this file the way the code model would")
                ToolTip.visible: hovered
                onClicked: choiceRow.choice.clearChoice()
            }
        }
    }

    // Which function the caret is in, and a way to go to another. A button
    // rather than a ComboBox, because what it opens is a tree and a combo's
    // popup is a list.
    QtcButton {
        id: outlineButton

        objectName: "outlineButton"
        anchors.verticalCenter: parent.verticalCenter
        visible: root.outline !== null && text !== ""
        text: root.outline ? root.outline.currentText : ""
        onClicked: outlinePopup.opened ? outlinePopup.close() : outlinePopup.open()

        Popup {
            id: outlinePopup

            objectName: "outlinePopup"
            y: outlineButton.height
            width: Math.max(outlineButton.width, 320)
            height: 320
            padding: 0

            TreeView {
                id: outlineTree

                objectName: "outlineTree"
                anchors.fill: parent
                clip: true
                model: root.outline ? root.outline.model : null

                delegate: TreeViewDelegate {
                    id: outlineRow

                    // TreeViewDelegate has row, column and model of its own,
                    // so nothing is required here - asking again is an
                    // override of what the base already answers.
                    // Picking a row is what the reader came here for, and the
                    // popup has said what it was for once that happens.
                    onClicked: {
                        // One column: an outline is a list of names, nested.
                        root.outline.activate(outlineTree.index(outlineRow.row, 0))
                        outlinePopup.close()
                    }
                }
            }
        }
    }

    Repeater {
        model: root.languageActions

        delegate: QtcButton {
            id: languageButton

            required property int index
            required property string actionText
            required property bool actionEnabled
            required property bool actionVisible
            required property string actionToolTip
            required property bool actionCheckable
            required property bool actionChecked
            required property var actionFont
            // An action can carry an icon instead of a text - the button that
            // brings back a minimized info bar is a warning sign and nothing
            // else - so a button bound only to the text would draw empty.
            required property string actionIcon
            // Null unless the action carries a menu, in which case this button
            // opens it rather than triggering - the widget editor's toolbar
            // does the same with a QToolButton set to InstantPopup.
            required property var actionMenu

            objectName: "languageToolBarButton"
            // Guarded: a Repeater's delegate has no parent while it is being
            // created, and an unguarded binding warns on every one of them.
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: languageButton.actionText
            iconSource: languageButton.actionIcon
            enabled: languageButton.actionEnabled
            visible: languageButton.actionVisible
            ToolTip.text: languageButton.actionToolTip
            ToolTip.visible: hovered && ToolTip.text !== ""
            checkable: languageButton.actionCheckable
            // The action's emphasis, not its whole font: what size and family
            // a tool bar button uses is the tool bar's business.
            labelBold: languageButton.actionFont.bold
            labelItalic: languageButton.actionFont.italic
            // Assigned rather than bound: activate() writes to checked on
            // every click of a checkable button, and a JavaScript assignment
            // destroys a binding. The action is what decides, so its answer is
            // taken every time it changes - including the first time, which is
            // when the delegate is given it.
            onActionCheckedChanged: languageButton.checked = languageButton.actionChecked
            onClicked: {
                if (languageButton.actionMenu)
                    languageMenu.popup()
                else
                    root.languageActions.trigger(languageButton.index)
            }

            Menu {
                id: languageMenu

                objectName: "languageToolBarMenu"

                Repeater {
                    model: languageButton.actionMenu

                    delegate: MenuItem {
                        required property int index
                        required property string actionText
                        required property bool actionEnabled

                        text: actionText
                        enabled: actionEnabled
                        onTriggered: languageButton.actionMenu.trigger(index)
                    }
                }
            }
        }
    }

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

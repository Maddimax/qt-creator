// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// A file, edited. Not TextEditorWidget - what a settings page needs of an
// editor is the file's own document, its colours, somewhere to type, and the
// completions its language server knows. See CodeDocument and CodeCompletion.
Item {
    id: root

    // The file to edit. Everything else follows from opening it.
    required property string filePath
    // What the text is, for the highlight definition. The document does not
    // say: a .clang-format file is YAML and named after neither.
    property string mimeType: ""
    // Whether a language server should be told about the file, which is what
    // makes completion answer anything.
    property bool useLanguageServer: false
    property bool readOnly: false

    // Whether the file was opened at all, and whether it has been edited since.
    readonly property bool opened: document.opened
    readonly property bool modified: document.modified

    // Writes the file back.
    function save(): bool { return document.save() }

    implicitHeight: Metrics.formTextAreaHeight

    CodeDocument {
        id: document

        document: edit.textDocument
        filePath: root.filePath
        useLanguageServer: root.useLanguageServer
    }

    CodeHighlighting {
        id: highlighting

        document: edit.textDocument
        mimeType: root.mimeType
    }

    CodeCompletion {
        id: completion

        codeDocument: document
    }

    // No language, so nothing is re-indented; it is what says how wide a Tab
    // is here, which is the global tab settings.
    CodeIndenting {
        id: indenting

        document: edit.textDocument
    }

    Frame {
        anchors.fill: parent

        RowLayout {
            anchors.fill: parent
            spacing: Spacing.GapHXs

            // The line numbers, placed against the lines themselves rather
            // than stacked: a wrapped line covers more than one row, and
            // counting rows would drift from counting lines.
            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: numbers.implicitWidth
                clip: true

                Column {
                    id: numbers

                    y: -view.contentY

                    Repeater {
                        model: edit.lineCount

                        Label {
                            required property int index

                            width: numbers.implicitWidth
                            height: edit.positionToRectangle(
                                        edit.text.split("\n", index + 1).join("\n").length).height
                            text: index + 1
                            font: highlighting.font
                            color: Tokens.textSubtle
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }

            ScrollView {
                id: view

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                // A ScrollView reports the implicit size of what it scrolls, so
                // the editor would grow to fit the file rather than scroll it.
                implicitWidth: 0
                implicitHeight: 0

                readonly property real contentY: ScrollBar.vertical.position * edit.height

                TextEdit {
                    id: edit

                    objectName: "codeEditorText"
                    readOnly: root.readOnly
                    selectByMouse: true
                    textFormat: TextEdit.PlainText
                    font: highlighting.font
                    color: highlighting.textColor
                    selectionColor: Tokens.accentDefault
                    selectedTextColor: Tokens.textOnAccent
                    width: view.availableWidth

                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Space
                                && (event.modifiers & Qt.ControlModifier)) {
                            completion.invoke(edit.cursorPosition)
                            event.accepted = true
                        } else if (event.key === Qt.Key_S
                                   && (event.modifiers & Qt.ControlModifier)) {
                            root.save()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape && completion.active) {
                            completion.cancel()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Tab) {
                            // Tab types an indent, and what an indent is is
                            // the code style's answer: a TextEdit types a tab
                            // character whatever the style says. Shift+Tab
                            // needs nothing: it already takes the indent back.
                            indenting.indentAt(edit.cursorPosition)
                            event.accepted = true
                        }
                    }
                }
            }
        }

        background: Rectangle {
            color: highlighting.backgroundColor
            radius: Spacing.RadiusS
            border.width: 1
            border.color: Tokens.strokeSubtle
        }
    }

    // What the provider offered, where the cursor is. A page that wants it
    // somewhere else can bind to CodeCompletion itself.
    Popup {
        id: proposals

        objectName: "completionPopup"
        visible: completion.active
        x: edit.positionToRectangle(edit.cursorPosition).x
        y: edit.positionToRectangle(edit.cursorPosition).y
           + edit.positionToRectangle(edit.cursorPosition).height
        padding: 0
        closePolicy: Popup.NoAutoClose

        contentItem: ListView {
            objectName: "completionList"
            implicitWidth: Metrics.lineEditWidth
            implicitHeight: Math.min(contentHeight, Metrics.formListHeight)
            model: completion.proposals
            clip: true
            keyNavigationEnabled: true
            currentIndex: 0

            delegate: ItemDelegate {
                required property int index
                required property string modelData

                width: ListView.view.width
                text: modelData
                highlighted: ListView.isCurrentItem
                onClicked: completion.apply(index)
            }
        }
    }
}

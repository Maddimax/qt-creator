// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// A string aspect edited as code: the same write-back discipline as any other
// text area, with the group's highlighting on top. See CodeHighlighting.
RowLayout {
    id: root

    required property Aspect aspect
    // What the text is, so that a highlight definition can be found.
    required property string mimeType

    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignTop
    }

    Frame {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Metrics.formTextAreaHeight

        ScrollView {
            anchors.fill: parent
            clip: true
            // A ScrollView reports the implicit size of what it scrolls, and
            // the Frame around it sizes to that - so a long snippet grew the
            // page to fit instead of scrolling. The size it gets is the one
            // the layout gives it.
            implicitWidth: 0
            implicitHeight: 0

            TextArea {
                id: edit

                objectName: "snippetEditorText"
                text: root.aspect?.value ?? ""
                enabled: root.aspect?.enabled ?? false
                readOnly: root.aspect?.readOnly ?? false
                textFormat: TextEdit.PlainText
                font: highlighting.font
                color: highlighting.textColor

                // Tab types an indent, and what an indent is is the code
                // style's answer: a TextEdit types a tab character whatever
                // the style says. Shift+Tab needs nothing: it already takes
                // the indent back.
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab) {
                        indenting.indentAt(edit.cursorPosition)
                        event.accepted = true
                    }
                }

                // Written back when focus leaves, not on every keystroke: an
                // aspect that reloads its value would fight the cursor.
                onEditingFinished: {
                    if (root.aspect && text !== root.aspect.value)
                        root.aspect.value = text
                }

                CodeHighlighting {
                    id: highlighting

                    document: edit.textDocument
                    mimeType: root.mimeType
                }

                // No language, so nothing is re-indented; it is what says how
                // wide a Tab is here, which is the global tab settings.
                CodeIndenting {
                    id: indenting

                    document: edit.textDocument
                }
            }
        }
    }
}

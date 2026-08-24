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

    readonly property string labelText: aspect?.labelText ?? ""
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

            TextArea {
                id: edit

                objectName: "snippetEditorText"
                text: root.aspect?.value ?? ""
                enabled: root.aspect?.enabled ?? false
                readOnly: root.aspect?.readOnly ?? false
                textFormat: TextEdit.PlainText
                font: highlighting.font
                color: highlighting.textColor

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
            }
        }
    }
}

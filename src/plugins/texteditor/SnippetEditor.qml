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
// text area, with the group's highlighting on top. There is no language here
// and so no code style, which is why nothing re-indents and why an indent is
// whatever the global tab settings say. See CodeBuffer.
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

    // The text, held apart from any view of it. The aspect owns the value;
    // this is the document a highlighter can work on, which a QString is not.
    CodeBuffer {
        id: buffer

        objectName: "snippetEditorBuffer"
        text: root.aspect?.value ?? ""
        mimeType: root.mimeType
    }

    CodeViewport {
        objectName: "snippetEditorText"
        source: buffer
        enabled: root.aspect?.enabled ?? false
        readOnly: root.aspect?.readOnly ?? false
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Metrics.formTextAreaHeight

        // Written back when focus leaves, not on every keystroke: an aspect
        // that reloads its value would fight the cursor.
        onEditingFinished: {
            if (root.aspect && buffer.text !== root.aspect.value)
                root.aspect.value = buffer.text
        }
    }
}

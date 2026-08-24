// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// What the code style being edited does to code. The text is the aspect's
// value, so typing in it is an ordinary aspect edit; the indenter re-runs
// whenever the style changes, which is the whole point of the thing.
ColumnLayout {
    id: root

    // A CodeStylePreviewAspect. Its extra properties come in separately: what
    // a page reaches through aspects.<name> is typed as a plain Aspect.
    required property Aspect aspect
    // What the code is, for looking up a highlight definition.
    required property string mimeType
    // Whose indenter re-indents it, and against which preferences.
    required property string languageId
    required property QtObject codeStyle

    // Whether the code is being indented. Worth reading in a test, and worth
    // knowing before blaming the layout.
    readonly property bool indenting: indenting_.indenting

    spacing: Spacing.GapVS
    Layout.fillWidth: true
    Layout.fillHeight: true

    Label {
        text: qsTr("Edit preview contents to see how the current settings are applied to "
                   + "custom code snippets. Changes in the preview do not affect the "
                   + "current settings.")
        wrapMode: Text.WordWrap
        color: Tokens.textMuted
        Layout.fillWidth: true
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

                objectName: "codeStylePreviewText"
                text: root.aspect?.value ?? ""
                enabled: root.aspect?.enabled ?? false
                textFormat: TextEdit.PlainText
                font: highlighting.font
                color: highlighting.textColor

                // Written back when focus leaves, not on every keystroke: the
                // indenter rewrites the document, and it must not do that
                // under the cursor.
                onEditingFinished: {
                    if (root.aspect && text !== root.aspect.value)
                        root.aspect.value = text
                }

                CodeHighlighting {
                    id: highlighting

                    document: edit.textDocument
                    mimeType: root.mimeType
                }

                CodeIndenting {
                    id: indenting_

                    document: edit.textDocument
                    languageId: root.languageId
                    codeStyle: root.codeStyle
                }
            }
        }
    }

    // The text arrives by binding, which can land after CodeIndenting has
    // attached; and it is replaced whenever the aspect's value changes.
    Component.onCompleted: indenting_.reindent()

    Connections {
        target: root.aspect
        function onVolatileValueChanged() { indenting_.reindent() }
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// What the code style being edited does to code. The text is an aspect's value,
// so typing in it is an ordinary aspect edit; the indenter re-runs whenever the
// style changes, which is the whole point of the thing. The page's aspects, not
// the language's, so every Code Style page gets the same preview; see
// CodeStyleAspect::CodeStyleAspect().
ColumnLayout {
    id: root

    // The page's NamedAspects. Preview is a CodeStylePreviewAspect, which
    // carries what a preview needs beyond the text; it is reached by name and
    // so is untyped here.
    required property var aspects

    // The aspect this draws. Declaring it is what makes a component count as a
    // delegate to the page census, and the Preview really is drawn here - by a
    // TextArea rather than by one of the stock delegates. Typed as Aspect
    // rather than as CodeStylePreviewAspect because a StringAspect subclass
    // cannot resolve for qmllint: TypedAspect<T> sits in its prototype chain
    // and a template instantiation has no metaobject to register.
    readonly property Aspect aspect: root.aspects.Preview

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

    // The text, held apart from any view of it. An aspect owns the value; this
    // is the document a highlighter and an indenter can work on, which a
    // QString is not.
    CodeBuffer {
        id: buffer

        objectName: "codeStylePreviewBuffer"
        text: root.aspects.Preview.value ?? ""
        mimeType: root.aspects.Preview.mimeType
    }

    CodeIndenting {
        id: indenting_

        source: buffer
        languageId: root.aspects.Preview.languageId
        codeStyle: root.aspects.Preview.codeStyle
    }

    CodeViewport {
        id: edit

        objectName: "codeStylePreviewText"
        readOnly: !(root.aspects.Preview.enabled ?? false)
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Metrics.formTextAreaHeight
        source: buffer

        // Written back when focus leaves, not on every keystroke: the indenter
        // rewrites the document, and it must not do that under the cursor.
        onEditingFinished: {
            if (buffer.text !== root.aspects.Preview.value)
                root.aspects.Preview.value = buffer.text
        }
    }

    RowLayout {
        spacing: Spacing.GapHXs
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        ButtonDelegate { aspect: root.aspects.ResetPreview; Layout.fillWidth: false }
        ButtonDelegate { aspect: root.aspects.FormatPreview; Layout.fillWidth: false }
    }

    // The text arrives by binding, which can land after CodeIndenting has
    // attached, and is replaced whenever the aspect's value changes.
    Component.onCompleted: indenting_.reindent()

    Connections {
        target: root.aspects.Preview

        function onVolatileValueChanged() { indenting_.reindent() }
        // Asked for when the text did not change but its layout should: the
        // Format button, for a language whose formatting is the indenter.
        function onReindentRequested() { indenting_.reindent() }
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui
import QtCreator.TextEditor

// Code, highlighted. A highlighter works on a QTextDocument and a TextEdit has
// one, so this needs none of TextEditorWidget: give it the text and what the
// text is, and CodeHighlighting colours it.
ScrollView {
    id: root

    // What to show, and what it is. The mime type is what a definition is
    // looked up by; without one the text still shows, unhighlighted.
    property alias text: edit.text
    required property string mimeType
    property bool readOnly: true

    // Whether a definition was found. Worth reading in a test, and worth
    // knowing before blaming the colours.
    readonly property bool highlighting: highlighting_.highlighting

    clip: true

    background: Rectangle {
        color: highlighting_.backgroundColor
        radius: Spacing.RadiusS
        border.width: 1
        border.color: Tokens.strokeSubtle
    }

    TextEdit {
        id: edit

        objectName: "codeViewText"
        readOnly: root.readOnly
        selectByMouse: true
        // Plain text: the highlighter sets the character formats, and rich text
        // would fight it for them.
        textFormat: TextEdit.PlainText
        // The editor's font and the scheme's plain-text colour, not the
        // form's: this is a view of code.
        font: highlighting_.font
        color: highlighting_.textColor
        selectionColor: Tokens.accentDefault
        selectedTextColor: Tokens.textOnAccent
        width: root.availableWidth

        CodeHighlighting {
            id: highlighting_

            document: edit.textDocument
            mimeType: root.mimeType
        }
    }
}

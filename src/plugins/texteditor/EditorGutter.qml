// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui
import QtCreator.TextEditor

// The line numbers beside a viewport. One item per line on screen rather than
// per line in the document, which is what keeps a million-line file the same
// price as a short one - the same rule the viewport itself follows.
Item {
    id: root

    required property TextViewport viewport

    // Room for a mark and for the highest number the document can reach,
    // measured in the font the numbers are drawn in. Both are reserved whether
    // or not anything is in them: a gutter that grew when the first error
    // arrived would move the text sideways under the reader.
    readonly property real markWidth: root.viewport.lineHeight

    implicitWidth: markWidth + widest.width + 2 * Spacing.PaddingHS

    TextMetrics {
        id: widest

        font: root.viewport.font
        text: String(Math.max(1, root.viewport.lineCount))
    }

    // What the line is marked with - an error, a warning, a breakpoint. One
    // per line on screen, the same as the numbers.
    Repeater {
        model: root.viewport.visibleLineCount

        delegate: Image {
            id: mark

            required property int index

            readonly property int line: root.viewport.firstVisibleLine + index
            // Not called "data": that is Item's default property, and a child
            // would then be assigned to this instead of to the item.
            readonly property var lineData: root.viewport.visibleLine(index)

            x: Spacing.PaddingHS
            y: line * root.viewport.lineHeight - root.viewport.scrollY
            width: root.markWidth
            height: root.viewport.lineHeight
            fillMode: Image.PreserveAspectFit

            source: mark.lineData.markIcon ?? ""
            visible: source !== ""

            ToolTip.text: mark.lineData.markToolTip ?? ""
            ToolTip.visible: hover.hovered && ToolTip.text !== ""

            HoverHandler { id: hover }
        }
    }

    Repeater {
        model: root.viewport.visibleLineCount

        delegate: Text {
            id: number

            required property int index

            // The document line this stands for, and where the viewport put it:
            // the same arithmetic, so the number sits on its own line at any
            // scroll offset rather than only at whole ones.
            readonly property int line: root.viewport.firstVisibleLine + index

            x: Spacing.PaddingHS + root.markWidth
            y: line * root.viewport.lineHeight - root.viewport.scrollY
            width: widest.width
            height: root.viewport.lineHeight

            text: String(line + 1)
            font: root.viewport.font
            color: Tokens.textMuted
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
        }
    }
}

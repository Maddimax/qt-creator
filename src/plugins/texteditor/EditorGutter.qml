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

    // Whether a line that starts a fold offers to close it. Off by default for
    // the same reason the numbers are: a settings preview is a few lines of
    // demonstration, and nothing there is foldable anyway.
    property bool showFoldMarkers: false

    // Whether the mark column answers a click. Off by default, for a preview
    // whose text is not a file - see CodeViewport.requestMarks.
    property bool requestMarks: false

    // Room for a mark and for the highest number the document can reach,
    // measured in the font the numbers are drawn in. Both are reserved whether
    // or not anything is in them: a gutter that grew when the first error
    // arrived would move the text sideways under the reader.
    readonly property real markWidth: root.viewport.lineHeight
    readonly property real foldWidth: root.showFoldMarkers ? root.viewport.lineHeight : 0
    // Hard against the text, which is where the widget editor puts it.
    readonly property real foldX: Spacing.PaddingHS + markWidth + widest.width
                                  + Spacing.PaddingHS

    implicitWidth: foldX + foldWidth

    TextMetrics {
        id: widest

        font: root.viewport.font
        text: String(Math.max(1, root.viewport.lineCount))
    }

    // The mark column, as something to click. Underneath what the gutter draws
    // rather than over it: the icons and the numbers accept no mouse buttons,
    // so a press reaches this regardless, and a mark's own hover tooltip is
    // not swallowed on the way.
    //
    // What a click here means is not the gutter's business. The document
    // decides, so that this column and the widget editor's answer alike.
    MouseArea {
        id: markColumn

        objectName: "gutterMarkColumn"

        x: Spacing.PaddingHS
        width: root.markWidth
        height: root.height
        enabled: root.requestMarks
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: (mouse) => {
            // In the viewport's coordinates, not the gutter's: the two are
            // laid out separately and only happen to line up.
            const at = markColumn.mapToItem(root.viewport, 0, mouse.y).y
            if (mouse.button === Qt.RightButton) {
                // Only when there is something to show. An empty menu drawn
                // as a sliver is what the widget editor avoids too.
                if (root.viewport.prepareMarkMenu(at))
                    markMenu.popup(markColumn.mapToItem(root, mouse.x, mouse.y))
                return
            }
            root.viewport.clickMarkColumn(at, mouse.modifiers)
        }
    }

    // How the line differs from the file on disk, drawn hard against the text
    // at the gutter's right edge - which is where the widget editor puts it.
    Repeater {
        model: root.viewport.visibleRows

        delegate: Rectangle {
            id: change

            required property int index
            required property var model

            readonly property int row: root.viewport.firstVisibleLine + index
            // Not "state": QQuickItem has one of those already.
            readonly property int changeState: change.model.changed

            x: root.width - width
            y: change.model.y - root.viewport.scrollY
            width: 2
            height: root.viewport.lineHeight
            visible: change.changeState !== 0
            color: change.changeState === TextViewport.Saved ? root.viewport.savedLineColor
                                                                : root.viewport.changedLineColor
        }
    }

    // What the line is marked with - an error, a warning, a breakpoint. One
    // per line on screen, the same as the numbers.
    Repeater {
        model: root.viewport.visibleRows

        delegate: Image {
            id: mark

            required property int index
            required property var model

            readonly property int row: root.viewport.firstVisibleLine + index
            readonly property var lineData: model

            x: Spacing.PaddingHS
            y: model.y - root.viewport.scrollY
            width: root.markWidth
            height: root.viewport.lineHeight
            fillMode: Image.PreserveAspectFit

            source: mark.lineData.markIcon
            visible: source !== ""

            ToolTip.text: mark.lineData.annotation
            ToolTip.visible: hover.hovered && ToolTip.text !== ""

            HoverHandler { id: hover }
        }
    }

    Repeater {
        model: root.viewport.visibleRows

        delegate: Text {
            id: number

            objectName: "gutterLineNumber"

            required property int index
            required property var model

            // The row the viewport put this on - the same arithmetic, so the
            // number sits on its own line at any scroll offset rather than
            // only at whole ones. What the line is *called* is a separate
            // question once folding is in play, and only the viewport can
            // answer it.
            readonly property int row: root.viewport.firstVisibleLine + index

            x: Spacing.PaddingHS + root.markWidth
            y: model.y - root.viewport.scrollY
            width: widest.width
            height: root.viewport.lineHeight

            // A wrapped line is numbered where it starts and nowhere else:
            // the continuation rows belong to the same line, and numbering
            // them again would be counting rows rather than lines.
            text: number.model.firstRowOfLine
                      ? String(number.model.displayNumber)
                      : ""
            font: root.viewport.font
            color: Tokens.textMuted
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    // What closes and opens a fold. Only foldable lines have one, so most rows
    // draw nothing - and the whole column is absent when nobody asked for it.
    Repeater {
        model: root.showFoldMarkers ? root.viewport.visibleRows : []

        delegate: Image {
            id: fold

            required property int index
            required property var model

            readonly property var lineData: model
            readonly property int row: root.viewport.firstVisibleLine + index

            x: root.foldX
            y: model.y - root.viewport.scrollY
            width: root.foldWidth
            height: root.viewport.lineHeight
            fillMode: Image.PreserveAspectFit

            source: fold.lineData.foldIcon
            visible: source !== ""

            TapHandler {
                // By the line's own number, not by the row: the viewport is
                // what knows which block a row came from, and it is the one
                // being asked.
                onTapped: root.viewport.toggleFold(fold.lineData.lineNumber)
            }
        }
    }

    // Hovering the folding column lights up the scopes around the line under
    // the pointer. The widget editor does this whenever the pointer is in that
    // column, whether or not the highlightBlocks setting is on - the setting
    // widens what counts as a hover rather than switching the highlight on.
    Item {
        id: foldColumn

        x: root.foldX
        width: root.foldWidth
        height: root.height
        visible: root.showFoldMarkers

        HoverHandler {
            id: foldHover

            // In the viewport's coordinates, not the gutter's: the two are
            // laid out separately and only happen to line up.
            onPointChanged: {
                if (!foldHover.hovered)
                    return
                const at = foldColumn.mapToItem(root.viewport, 0, foldHover.point.position.y)
                root.viewport.highlightScopeAt(at.y)
            }
            onHoveredChanged: {
                if (!foldHover.hovered)
                    root.viewport.clearScopeHighlight()
            }
        }
    }

    // What the mark column offers on a right click - a bookmark, and whatever
    // the debugger and the analysers have to say about the line.
    ActionMenu {
        id: markMenu

        objectName: "gutterMarkMenu"

        actions: root.viewport.markActions
    }
}

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

        // A mark can be dragged to another line - that is how a breakpoint is
        // moved. Whether the press took one up, and whether the pointer has
        // since moved far enough to call it a drag rather than a click.
        property bool holdingMark: false
        property bool draggingMark: false
        property real pressedY: 0

        function viewportY(y) {
            // In the viewport's coordinates, not the gutter's: the two are
            // laid out separately and only happen to line up.
            return markColumn.mapToItem(root.viewport, 0, y).y
        }

        onPressed: (mouse) => {
            markColumn.draggingMark = false
            markColumn.holdingMark = false
            if (mouse.button !== Qt.LeftButton)
                return
            markColumn.pressedY = mouse.y
            markColumn.holdingMark = root.viewport.beginMarkDrag(markColumn.viewportY(mouse.y))
        }

        onPositionChanged: (mouse) => {
            if (!markColumn.holdingMark || markColumn.draggingMark)
                return
            if (Math.abs(mouse.y - markColumn.pressedY) >= Qt.styleHints.startDragDistance)
                markColumn.draggingMark = true
        }

        onReleased: (mouse) => {
            if (!markColumn.holdingMark)
                return
            markColumn.holdingMark = false
            if (markColumn.draggingMark)
                root.viewport.endMarkDrag(markColumn.viewportY(mouse.y))
            else
                root.viewport.cancelMarkDrag()
        }

        onCanceled: {
            markColumn.holdingMark = false
            markColumn.draggingMark = false
            root.viewport.cancelMarkDrag()
        }

        onClicked: (mouse) => {
            // A drag ends in a release and a click both; what moved a mark is
            // not also a click on the line it was dropped on.
            if (markColumn.draggingMark) {
                markColumn.draggingMark = false
                return
            }
            const at = markColumn.viewportY(mouse.y)
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

    // The numbers, as something to select with. Beside the mark column rather
    // than over it, so that a press in one is never a press in the other, and
    // underneath what the gutter draws for the same reason that one is.
    MouseArea {
        id: numberColumn

        objectName: "gutterNumberColumn"

        x: Spacing.PaddingHS + root.markWidth
        width: Math.max(0, root.foldX - x)
        height: root.height
        acceptedButtons: Qt.LeftButton

        // Where the pointer was last seen, so the timer below can go on
        // taking lines while nothing is moving.
        property real dragY: 0

        function viewportY(y: real): real {
            return numberColumn.mapToItem(root.viewport, 0, y).y
        }

        onPressed: (mouse) => {
            numberColumn.dragY = mouse.y
            root.viewport.beginLineSelection(numberColumn.viewportY(mouse.y))
        }
        onPositionChanged: (mouse) => {
            numberColumn.dragY = mouse.y
            root.viewport.extendLineSelection(numberColumn.viewportY(mouse.y))
            // Off the top or the bottom, the drag is asking for lines that are
            // not on screen yet, so the view has to go and get them - and keep
            // going while the pointer stays out there.
            lineScroll.running = mouse.y < 0 || mouse.y > numberColumn.height
        }
        onReleased: lineScroll.stop()
        onCanceled: lineScroll.stop()

        Timer {
            id: lineScroll

            objectName: "gutterAutoScroll"
            interval: 50
            repeat: true

            onTriggered: {
                const past = numberColumn.dragY < 0
                           ? numberColumn.dragY
                           : numberColumn.dragY - numberColumn.height
                // A line per tick, up to five the further out it is, which is
                // what the text area does for the same gesture. Not what makes
                // the drag advance at all - setTextCursor() brings the view
                // along by itself - only how fast it travels when the pointer
                // is a long way out, which is why no assertion below separates
                // it. Taking it out makes a long drag crawl at a line every
                // fifty milliseconds.
                const lines = Math.min(5, 1 + Math.abs(past) / root.viewport.lineHeight)
                root.viewport.scrollY += Math.sign(past) * root.viewport.lineHeight * lines
                // extendLineSelection() clamps to what is laid out, so after
                // the scroll this is the newly arrived first or last line.
                root.viewport.extendLineSelection(numberColumn.viewportY(numberColumn.dragY))
            }
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

            objectName: "gutterChangeBar"

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

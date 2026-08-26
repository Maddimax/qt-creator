// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui
import QtCreator.TextEditor

// A file, shown by the scene graph. CodeView does the same job with a TextEdit,
// which lays out the whole document; this lays out what is on screen, so it is
// the one to reach for when the file might be large.
//
// Everything that is not text is drawn here rather than in TextViewport: the
// background, the caret and the scroll bar are QML's job, and the viewport only
// says where they go. It does not edit - there is no key handling and no
// cursor movement beyond a click.
Item {
    id: root

    // What to show. A CodeDocument for a file, a CodeBuffer for text that was
    // never one; either can be given inline. Not a path, because a preview's
    // text is an aspect's value and has no path.
    required property CodeSource source
    // Whether typing does anything. A view until told otherwise, so that
    // showing a file cannot accidentally change it.
    property alias readOnly: viewport.readOnly

    // Whether the lines are numbered. Off by default: a settings preview is a
    // few lines of demonstration and numbering them says nothing, while an
    // editor without them is not one.
    property bool showLineNumbers: false

    // Whether the gutter offers to fold. Separate from the numbers because a
    // view can want one without the other, which is what the display settings
    // let a user say.
    property bool showFoldMarkers: false

    // What a right click offers, or null for a view that offers nothing - a
    // settings preview has no Find Usages to give.
    property ActionModel contextActions: null

    // Focus has left, so whatever was being typed is finished. A page that
    // writes the text somewhere else uses this rather than every keystroke:
    // re-indenting rewrites the document, and it must not do that under the
    // cursor.
    signal editingFinished()

    // Where the caret is, and what is selected, as document positions.
    // Whether the caret is in here. The viewport is a focus scope, so the root
    // item's own activeFocus stays false the whole time someone is typing.
    readonly property alias editing: viewport.activeFocus

    readonly property alias cursorPosition: viewport.cursorPosition
    readonly property alias selectionStart: viewport.selectionStart
    readonly property alias selectionEnd: viewport.selectionEnd

    implicitHeight: Metrics.formTextAreaHeight

    Rectangle {
        anchors.fill: parent
        color: viewport.backgroundColor
        radius: Spacing.RadiusS
        border.width: 1
        border.color: Tokens.strokeSubtle

        // The line the caret is on, drawn first so it is behind both the
        // numbers and the text. Its colour is transparent unless the scheme
        // asks for one, so a scheme with no current-line highlight gets none
        // rather than a black bar.
        Rectangle {
            id: currentLine

            objectName: "currentLineHighlight"
            color: viewport.currentLineColor
            x: 0
            width: parent.width
            // cursorRectangle is in the viewport's coordinates and the viewport
            // is inset, so the highlight has to be moved by the same inset or
            // it sits a margin above the line it is meant to be on.
            y: viewport.y + viewport.cursorRectangle.y
            height: viewport.lineHeight
            // An empty caret rect is how the viewport says the position is
            // scrolled off screen; there is no line to highlight then.
            visible: viewport.cursorRectangle.height > 0
        }

        EditorGutter {
            id: gutter

            objectName: "codeGutter"
            viewport: viewport
            showFoldMarkers: root.showFoldMarkers
            visible: root.showLineNumbers || root.showFoldMarkers
            // No width when it is not shown, so the text starts where it would
            // have without a gutter rather than indented by an invisible one.
            width: visible ? implicitWidth : 0
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: Spacing.PaddingHXs
        }

        // Behind the viewport rather than over it. Clicking text is the
        // fallback for the whole area, so anything inside the viewport that
        // wants a click of its own - the box standing in for a fold - has to
        // be in front of this. TextViewport accepts no mouse buttons itself,
        // so a press that lands on none of them falls through to here.
        MouseArea {
            anchors.fill: viewport
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.IBeamCursor

            onPressed: (mouse) => {
                viewport.forceActiveFocus()
                const position = viewport.positionAt(mouse.x, mouse.y)
                if (mouse.button === Qt.RightButton) {
                    // Right-clicking inside a selection acts on it, so the
                    // caret only moves when the click lands outside one.
                    const inSelection = viewport.selectionStart !== viewport.selectionEnd
                                     && position >= Math.min(viewport.selectionStart,
                                                             viewport.selectionEnd)
                                     && position <= Math.max(viewport.selectionStart,
                                                             viewport.selectionEnd)
                    if (!inSelection) {
                        viewport.cursorPosition = position
                        viewport.selectionStart = position
                        viewport.selectionEnd = position
                    }
                    if (root.contextActions) {
                        root.contextActions.refresh()
                        contextMenu.popup(mouse.x, mouse.y)
                    }
                    return
                }
                // The third click of a triple click arrives as a plain press -
                // Qt only ever reports one double click - so this is where a
                // triple click has to be recognised, before the press below
                // collapses the selection the second click made.
                if (tripleClick.running) {
                    tripleClick.stop()
                    viewport.selectLineAt(position)
                    return
                }
                viewport.cursorPosition = position
                // A press starts a selection of nothing rather than clearing
                // it, so that the drag below has an anchor to grow from.
                viewport.selectionStart = position
                viewport.selectionEnd = position
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const position = viewport.positionAt(mouse.x, mouse.y)
                viewport.cursorPosition = position
                viewport.selectionEnd = position
            }
            onDoubleClicked: (mouse) => {
                viewport.selectWordAt(viewport.positionAt(mouse.x, mouse.y))
                tripleClick.restart()
            }

            Timer {
                id: tripleClick

                interval: Application.styleHints.mouseDoubleClickInterval
            }
        }

        TextViewport {
            id: viewport

            objectName: "codeViewport"
            anchors.fill: parent
            anchors.margins: Spacing.PaddingHXs
            anchors.leftMargin: Spacing.PaddingHXs + gutter.width
            document: root.source
            // Keys go to the scene's active focus item. The viewport is a focus
            // scope, so focusing the root would stop one level short of it.
            focus: true

            onActiveFocusChanged: {
                if (!activeFocus)
                    root.editingFinished()
            }

            // What comes after a line's text: the box standing in for what a
            // fold hides, then what a mark on the line says. One delegate for
            // both, in a Row, because they land in the same place and a folded
            // line with an error on it would otherwise draw them on top of
            // each other. The viewport reports each line's natural width, so
            // they start where the text actually ends rather than at a column.
            Repeater {
                model: viewport.visibleLines

                delegate: Row {
                    id: trailing

                    required property int index
                    required property var modelData

                    readonly property var lineData: modelData
                    readonly property int row: viewport.firstVisibleLine + index

                    x: lineData.width + Spacing.GapHM - viewport.scrollX
                    y: row * viewport.lineHeight - viewport.scrollY
                    height: viewport.lineHeight
                    spacing: Spacing.GapHM

                    Rectangle {
                        width: replacement.implicitWidth + 2 * Spacing.PaddingHS
                        height: viewport.lineHeight
                        visible: replacement.text !== ""
                        color: "transparent"
                        border.color: Tokens.textMuted
                        radius: 3

                        Text {
                            id: replacement

                            anchors.centerIn: parent
                            text: trailing.lineData.foldReplacement ?? ""
                            font: viewport.font
                            color: Tokens.textMuted
                        }

                        // The box is the other way to open a fold: the gutter
                        // marker is far from what the reader is looking at,
                        // and the widget editor opens on this too.
                        TapHandler {
                            onTapped: viewport.toggleFold(trailing.lineData.lineNumber)
                        }
                    }

                    Text {
                        id: annotation

                        height: viewport.lineHeight
                        verticalAlignment: Text.AlignVCenter

                        text: trailing.lineData.annotation ?? ""
                        visible: text !== ""
                        font: viewport.font
                        color: Tokens.textMuted
                        elide: Text.ElideRight
                        // Never wider than what is left of the line, so a long
                        // message does not draw past the edge of the editor.
                        width: Math.max(0, viewport.width - trailing.x - x)
                    }
                }
            }

            // The caret. A Rectangle because that is what it is; the viewport
            // draws text and says where the caret belongs, and an empty rect
            // is how it says the position is scrolled off screen.
            Rectangle {
                id: caret

                x: viewport.cursorRectangle.x
                y: viewport.cursorRectangle.y
                width: viewport.cursorRectangle.width
                height: viewport.cursorRectangle.height
                visible: viewport.cursorRectangle.width > 0 && viewport.activeFocus
                color: Tokens.textDefault

                Timer {
                    running: caret.visible
                    repeat: true
                    // The platform's own blink rate, halved because one blink
                    // is two of these. Zero or less means do not blink at all.
                    interval: Application.styleHints.cursorFlashTime / 2
                    onTriggered: caret.opacity = caret.opacity > 0 ? 0 : 1
                }
            }
        }

        WheelHandler {
            target: null
            onWheel: (event) => {
                // Ctrl and the wheel is a zoom rather than a scroll, and the
                // viewport is what knows whether the user allows it.
                if (event.modifiers & Qt.ControlModifier) {
                    viewport.zoomBy(event.angleDelta.y / 120)
                    return
                }
                viewport.scrollY -= event.angleDelta.y / 120 * viewport.lineHeight * 3
            }
        }

        ScrollBar {
            id: verticalScrollBar

            objectName: "verticalScrollBar"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            orientation: Qt.Vertical
            policy: size < 1 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            size: viewport.contentHeight > 0
                  ? viewport.height / viewport.contentHeight
                  : 1
            // The viewport says where the handle goes, and while the handle
            // is held it says where to scroll to. Both directions at once is
            // safe here: the drag writes position from C++, which updates the
            // value without destroying the binding the way a JS assignment
            // would, so the two settle rather than fight.
            position: viewport.contentHeight > 0
                      ? viewport.scrollY / viewport.contentHeight
                      : 0
            onPositionChanged: {
                if (pressed)
                    viewport.scrollY = position * viewport.contentHeight
            }
        }
    }

    // The right-click menu. Qt Creator's menus are QActions assembled by the
    // ActionManager out of every plugin that wants a say, so this lists what
    // that produced rather than naming any of it - see QtcQuick::ActionModel.
    Menu {
        id: contextMenu

        objectName: "editorContextMenu"

        Repeater {
            model: root.contextActions

            delegate: MenuItem {
                id: entry

                required property int index
                required property string actionText
                required property string actionShortcut
                required property bool actionEnabled
                required property bool actionVisible
                required property bool actionCheckable
                required property bool actionChecked
                required property bool actionSeparator

                text: entry.actionSeparator ? "" : entry.actionText
                enabled: !entry.actionSeparator && entry.actionEnabled
                visible: entry.actionVisible
                checkable: entry.actionCheckable
                checked: entry.actionChecked

                // A separator is an entry with nothing in it and a rule drawn
                // through it, rather than a MenuSeparator: a Repeater's
                // delegate is one type, and a Menu treats its MenuItems
                // specially enough that swapping the type is not worth it.
                //
                // A Binding rather than a conditional: the other arm would be
                // "whatever the style says", and there is no way to write that
                // - assigning undefined to a double is an error, not a reset.
                Binding on implicitHeight {
                    when: entry.actionSeparator
                    value: Spacing.GapVM
                    restoreMode: Binding.RestoreBindingOrValue
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    visible: entry.actionSeparator
                    color: Tokens.strokeSubtle
                }

                onTriggered: root.contextActions.trigger(entry.index)
            }
        }
    }
}

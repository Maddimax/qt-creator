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

    // Whether the line the caret is on is marked. Off, like the display setting
    // it follows: an editor that highlights the current line when the user did
    // not ask for it is drawing something Creator does not.
    property bool highlightCurrentLine: false

    // Whether a text mark's message is written after the line it is on. On,
    // like the display setting it follows.
    property bool showAnnotations: true

    // What a right click offers, or null for a view that offers nothing - a
    // settings preview has no Find Usages to give.
    property ActionModel contextActions: null

    // Whether long lines are broken across rows. Off for a preview, and the
    // viewport's own default, because with it on the cost of showing a file
    // stops being what is on screen.
    property alias wrapping: viewport.wrapping

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
            // The text, and not the gutter beside it: the widget editor draws
            // this inside its viewport, and the gutter says which line is
            // current by the colour of the number rather than by a band
            // through it.
            x: viewport.x
            width: viewport.width
            // cursorRectangle is in the viewport's coordinates and the viewport
            // is inset, so the highlight has to be moved by the same inset or
            // it sits a margin above the line it is meant to be on.
            y: viewport.y + viewport.cursorRectangle.y
            height: viewport.lineHeight
            // An empty caret rect is how the viewport says the position is
            // scrolled off screen; there is no line to highlight then.
            visible: root.highlightCurrentLine && viewport.cursorRectangle.height > 0
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
            id: textArea

            // Where the pointer was last seen, so that the timer below can go
            // on extending the selection while nothing is moving.
            property real dragX: 0
            property real dragY: 0

            anchors.fill: viewport
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            // Off the screen while the user types, where the platform and the
            // settings both allow it. The viewport says when; the shape is the
            // form's to set.
            cursorShape: viewport.mouseHidden ? Qt.BlankCursor : Qt.IBeamCursor
            hoverEnabled: true

            // Set while a press is sitting on a selection without having moved
            // far enough to be a drag: -1 when there is no such press.
            property int pendingDragAt: -1
            // Set while a drag that began with Alt is taking a rectangle
            // of text rather than a run of it.
            property bool blockSelecting: false
            property real pressX: 0
            property real pressY: 0
            // A drag of ours that came back down on this same viewport. Its own
            // drop has already taken the text from where it was, so the move
            // must not be undone a second time when the drag finishes.
            property bool droppedOnSelf: false

            function isInSelection(position: int): bool {
                return viewport.selectionStart !== viewport.selectionEnd
                    && position >= Math.min(viewport.selectionStart, viewport.selectionEnd)
                    && position <= Math.max(viewport.selectionStart, viewport.selectionEnd)
            }

            function extendSelection(): void {
                const position = viewport.positionAt(textArea.dragX, textArea.dragY)
                viewport.cursorPosition = position
                viewport.selectionEnd = position
            }

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
                // Alt+click puts another caret there rather than moving the
                // one there is, and Alt+Shift takes the rectangle from where
                // the caret is to where the click landed. Not with Ctrl,
                // which is the modifier that follows a symbol - and takes Alt
                // to mean "in a split".
                if ((mouse.modifiers & Qt.AltModifier)
                        && !(mouse.modifiers & Qt.ControlModifier)) {
                    if (mouse.modifiers & Qt.ShiftModifier) {
                        viewport.anchorBlockSelection(-1)
                        viewport.selectBlockTo(mouse.x, mouse.y)
                    } else {
                        // The caret stays where it was: this only records
                        // where a drag would start from.
                        viewport.addCaretAt(position)
                        viewport.anchorBlockSelection(position)
                        textArea.blockSelecting = true
                    }
                    return
                }
                // Ctrl+click follows the symbol under the pointer rather than
                // putting the caret there. Which modifiers mean that, and
                // whether the setting allows it at all, is the viewport's to
                // say rather than restated here.
                if (viewport.isMouseNavigation(mouse.modifiers)
                        && viewport.followSymbolAt(
                            position,
                            viewport.opensInNextSplit((mouse.modifiers & Qt.AltModifier) !== 0))) {
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
                // A press inside a selection may be picking it up to drag it
                // somewhere, so the selection has to survive until the press
                // turns out to be an ordinary click after all.
                if (textArea.isInSelection(position)) {
                    textArea.pendingDragAt = position
                    textArea.pressX = mouse.x
                    textArea.pressY = mouse.y
                    return
                }
                viewport.cursorPosition = position
                // A press starts a selection of nothing rather than clearing
                // it, so that the drag below has an anchor to grow from.
                viewport.selectionStart = position
                viewport.selectionEnd = position
            }
            onPositionChanged: (mouse) => {
                // A mouse that has moved is one the user is looking for again.
                viewport.showMouse()
                if (!pressed)
                    return
                if (textArea.pendingDragAt >= 0) {
                    const far = Math.abs(mouse.x - textArea.pressX)
                              + Math.abs(mouse.y - textArea.pressY)
                    if (far >= Application.styleHints.startDragDistance) {
                        textArea.pendingDragAt = -1
                        textArea.droppedOnSelf = false
                        dragProxy.Drag.startDrag(Qt.CopyAction | Qt.MoveAction)
                    }
                    return
                }
                // Alt held while dragging takes a rectangle rather than a
                // run of text, which is a selection per line rather than one
                // that wraps round the ends of them.
                if (textArea.blockSelecting) {
                    viewport.selectBlockTo(mouse.x, mouse.y)
                    autoScroll.running = mouse.y < 0 || mouse.y > textArea.height
                    return
                }
                textArea.dragX = mouse.x
                textArea.dragY = mouse.y
                textArea.extendSelection()
                // Off the top or the bottom, the drag is asking for text that
                // is not on screen yet, so the view has to go and get it -
                // and keep going while the pointer stays out there.
                autoScroll.running = mouse.y < 0 || mouse.y > textArea.height
            }
            onReleased: {
                autoScroll.stop()
                textArea.blockSelecting = false
                // The press never became a drag, so it was a click, and a click
                // inside a selection puts the caret where it landed.
                if (textArea.pendingDragAt >= 0) {
                    viewport.cursorPosition = textArea.pendingDragAt
                    viewport.selectionStart = textArea.pendingDragAt
                    viewport.selectionEnd = textArea.pendingDragAt
                    textArea.pendingDragAt = -1
                }
            }
            onCanceled: {
                autoScroll.stop()
                textArea.blockSelecting = false
                textArea.pendingDragAt = -1
            }

            Timer {
                id: autoScroll

                objectName: "editorAutoScroll"
                interval: 50
                repeat: true

                onTriggered: {
                    const past = textArea.dragY < 0
                               ? textArea.dragY
                               : textArea.dragY - textArea.height
                    // A line per tick, up to five the further out it is, so
                    // that a long way to go does not take a long time.
                    const lines = Math.min(5, 1 + Math.abs(past) / viewport.lineHeight)
                    viewport.scrollY += Math.sign(past) * viewport.lineHeight * lines
                    // positionAt() clamps to what is laid out, so after the
                    // scroll this is the newly arrived first or last line.
                    textArea.extendSelection()
                }
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

        // Carries the selection out of the editor. It draws nothing and is
        // never positioned: what it exists for is the Drag attached property,
        // which has to be attached to an Item.
        Item {
            id: dragProxy

            objectName: "editorDragProxy"
            Drag.dragType: Drag.Automatic
            Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
            Drag.mimeData: ({ "text/plain": viewport.selectedText })

            // A move means the text is now somewhere else, so it stops being
            // here - unless it came back down on this same viewport, where the
            // drop has already taken it from where it was.
            Drag.onDragFinished: (dropAction) => {
                if (dropAction === Qt.MoveAction && !textArea.droppedOnSelf)
                    viewport.removeSelectedText()
                textArea.droppedOnSelf = false
            }
        }

        DropArea {
            id: textDrop

            objectName: "editorDropArea"
            anchors.fill: viewport
            // Files are opened, not inserted, so a URL is somebody else's to
            // handle and this has to keep its hands off it.
            onEntered: (drag) => {
                if (drag.hasUrls || !drag.hasText || viewport.readOnly)
                    drag.accepted = false
            }
            onDropped: (drop) => {
                if (drop.hasUrls || !drop.hasText || viewport.readOnly) {
                    drop.accepted = false
                    return
                }
                const fromHere = drop.source === dragProxy
                textArea.droppedOnSelf = fromHere
                viewport.dropText(drop.text, drop.x, drop.y, fromHere)
                viewport.forceActiveFocus()
                drop.accept(fromHere ? Qt.MoveAction : drop.proposedAction)
            }
        }

        TextViewport {
            id: viewport

            objectName: "codeViewport"
            // Narrowed from both sides when the content is not allowed the
            // whole width. The gutter stays where it is: it is the text that
            // is being centred, which is what the widget editor insets too.
            readonly property real contentInset: root.width
                                                 * (100 - viewport.contentWidthPercent) / 200

            anchors.fill: parent
            anchors.margins: Spacing.PaddingHXs
            anchors.leftMargin: Spacing.PaddingHXs + gutter.width + contentInset
            anchors.rightMargin: Spacing.PaddingHXs + contentInset + minimap.width
            document: root.source
            // Keys go to the scene's active focus item. The viewport is a focus
            // scope, so focusing the root would stop one level short of it.
            focus: true

            onActiveFocusChanged: {
                if (!activeFocus)
                    root.editingFinished()
            }

            // What the language offers to finish the word being typed. Asked
            // for by Ctrl+Space and nothing else for now: offering unbidden
            // needs a view on how often to ask a language that may be slow.
            CompletionPopup {
                id: completions

                objectName: "completionPopup"
                completions: []
                prefix: ""
                x: viewport.cursorRectangle.x
                y: viewport.cursorRectangle.y + viewport.lineHeight

                onAccepted: (completion) => viewport.applyCompletion(completion)
            }

            // The signature of the call being typed. Not a list to choose
            // from and nothing to accept: it says what the call takes while
            // the arguments are being written, and goes when they are done.
            // Every overload at once rather than one with a way to cycle -
            // the whole point is to read them, and a list that can be read
            // needs no cycling.
            Popup {
                id: functionHint

                objectName: "functionHintPopup"

                property var signatures: []
                // Which argument the caret is in. Not drawn yet; it is what
                // the widget editor emboldens, and it is carried here so that
                // doing the same needs no new plumbing.
                property int activeArgument: -1

                visible: signatures.length > 0
                x: viewport.cursorRectangle.x
                y: viewport.cursorRectangle.y + viewport.lineHeight
                padding: Spacing.PaddingHS
                closePolicy: Popup.NoAutoClose

                contentItem: Column {
                    objectName: "functionHintText"

                    Repeater {
                        model: functionHint.signatures

                        delegate: Text {
                            required property string modelData

                            text: modelData
                            textFormat: Text.RichText
                            font: viewport.font
                            color: Tokens.textDefault
                        }
                    }
                }
            }

            // What the language would fix here. Not a CompletionPopup: that
            // one narrows a list by what has been typed and shows nothing
            // when nothing has been, which is right for finishing a word and
            // wrong for a list of fixes that is complete as it stands.
            Popup {
                id: quickFixes

                objectName: "quickFixPopup"

                // The fixes on offer, in the order the language ranked them,
                // which is the order applyQuickFix() counts in.
                property var fixes: []

                visible: fixes.length > 0
                x: viewport.cursorRectangle.x
                y: viewport.cursorRectangle.y + viewport.lineHeight
                padding: 0
                closePolicy: Popup.NoAutoClose

                contentItem: ListView {
                    objectName: "quickFixList"

                    implicitWidth: Metrics.lineEditWidth
                    implicitHeight: Math.min(contentHeight, Metrics.formListHeight)
                    model: quickFixes.fixes
                    clip: true
                    keyNavigationEnabled: true
                    currentIndex: 0

                    delegate: ItemDelegate {
                        required property int index
                        required property string modelData

                        width: ListView.view.width
                        text: modelData
                        highlighted: ListView.isCurrentItem
                        // By position in the list, which is what the proposal
                        // counts in - two fixes may read the same.
                        onClicked: viewport.applyQuickFix(index)
                    }
                }
            }

            Connections {
                target: viewport

                function onCompletionRequested(): void {
                    viewport.requestCompletions()
                }

                function onQuickFixesAvailable(fixes: list<string>): void {
                    quickFixes.fixes = fixes
                }

                function onFunctionHintAvailable(signatures: list<string>,
                                                 activeArgument: int): void {
                    functionHint.signatures = signatures
                    functionHint.activeArgument = activeArgument
                }

                // The answer, whenever it comes: the provider every text file
                // gets works in a thread, so this is a second event and not a
                // return value.
                function onCompletionsAvailable(candidates: list<string>,
                                                prefix: string): void {
                    completions.completions = candidates
                    completions.prefix = prefix
                    completions.offer()
                }
            }

            // The area past the right margin, tinted where the settings ask.
            // Behind the text like the indent guides, and for the same reason.
            Rectangle {
                objectName: "marginArea"

                z: -1
                x: viewport.marginX - viewport.scrollX
                width: Math.max(0, viewport.width - x)
                height: viewport.height
                visible: viewport.tintMarginArea && viewport.marginX >= 0 && width > 0
                color: viewport.marginAreaColor
            }

            // And the line itself, which is drawn whether or not the area past
            // it is tinted.
            Rectangle {
                objectName: "marginLine"

                z: -1
                x: viewport.marginX - viewport.scrollX
                width: 1
                height: viewport.height
                visible: viewport.marginX >= 0 && x < viewport.width
                color: viewport.marginLineColor
            }

            // The wrapped-line marker, on the rows that continue a line.
            Repeater {
                model: viewport.visibleRows

                delegate: Text {
                    required property int index
                    required property var model

                    readonly property int row: viewport.firstVisibleLine + index

                    z: -1
                    visible: text !== ""
                    text: model.breakMarker
                    x: model.breakMarkerX - viewport.scrollX
                    y: model.y - viewport.scrollY
                    height: viewport.lineHeight
                    verticalAlignment: Text.AlignVCenter
                    font: viewport.font
                    color: viewport.indentGuideColor
                }
            }

            // Spaces and tabs, where the reader asked to see them. A dot in
            // the middle of a space and a rule across a tab, which is what the
            // widget editor gets from QTextLine::draw() - the scene graph
            // draws glyphs and would otherwise draw nothing here.
            Repeater {
                model: viewport.visibleRows

                delegate: Item {
                    id: whitespaceRow

                    required property int index
                    required property var model

                    readonly property var marks: model.whitespace
                    readonly property int row: viewport.firstVisibleLine + index

                    z: -1
                    y: whitespaceRow.model.y - viewport.scrollY
                    height: viewport.lineHeight
                    visible: whitespaceRow.marks.length > 0

                    Repeater {
                        model: whitespaceRow.marks

                        delegate: Rectangle {
                            required property var modelData

                            readonly property bool isTab: modelData.tab ?? false
                            // A dot sits in the middle of the space it stands
                            // for; a tab is drawn across the width it took.
                            x: (isTab ? modelData.x + 1
                                      : modelData.x + modelData.width / 2 - 1)
                               - viewport.scrollX
                            y: viewport.lineHeight / 2
                            width: isTab ? Math.max(1, modelData.width - 2) : 2
                            height: isTab ? 1 : 2
                            color: viewport.indentGuideColor
                        }
                    }
                }
            }

            // The indent guides, behind the text rather than over it: a
            // negative z puts a child under its parent's own drawing, and the
            // parent here is what paints the glyphs.
            Repeater {
                model: viewport.visibleRows

                delegate: Item {
                    id: guideRow

                    required property int index
                    required property var model

                    readonly property var lineData: model
                    readonly property int row: viewport.firstVisibleLine + index

                    z: -1
                    y: guideRow.model.y - viewport.scrollY
                    height: viewport.lineHeight
                    visible: guideRow.lineData.indentGuides > 0

                    Repeater {
                        model: guideRow.lineData.indentGuides

                        delegate: Rectangle {
                            required property int index

                            // Named because a one pixel wide item is not only
                            // ever a guide - the caret is one too, and sits on
                            // whichever row the reader left it on.
                            objectName: "indentGuide"

                            x: index * viewport.indentWidth - viewport.scrollX
                            width: 1
                            height: viewport.lineHeight
                            color: viewport.indentGuideColor
                        }
                    }
                }
            }

            // The box standing in for what a fold hides. The viewport reports
            // each line's natural width, so it starts where the text actually
            // ends rather than at a column.
            Repeater {
                model: viewport.visibleRows

                delegate: Row {
                    id: trailing

                    required property int index
                    required property var model

                    readonly property var lineData: model
                    readonly property int row: viewport.firstVisibleLine + index

                    x: lineData.width + Spacing.GapHM - viewport.scrollX
                    y: model.y - viewport.scrollY
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
                            text: trailing.lineData.foldReplacement
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
                }
            }

            // What a mark on the line says. Placed where the viewport puts it
            // rather than after the text: the display settings choose between
            // the end of the line, the right margin and the right edge, and
            // only the viewport knows where those are.
            Repeater {
                model: viewport.visibleRows

                delegate: Text {
                    id: annotation

                    required property int index
                    required property var model

                    readonly property var lineData: model
                    readonly property int row: viewport.firstVisibleLine + index

                    x: annotation.lineData.annotationX
                    y: annotation.model.annotationY - viewport.scrollY
                    height: viewport.lineHeight
                    verticalAlignment: Text.AlignVCenter

                    text: annotation.lineData.annotation
                    visible: root.showAnnotations && text !== ""
                    font: viewport.font
                    color: Tokens.textMuted
                    elide: Text.ElideRight
                    // Never past the right edge, so a long message is cut
                    // rather than drawn outside the editor.
                    width: Math.max(0, viewport.width - x)
                }
            }

            // The matching bracket, pulsed once where it stands. The widget
            // editor grows the character to half again its size and back over
            // a quarter of a second, which is what this is.
            Rectangle {
                id: bracketPulse

                objectName: "bracketPulse"

                visible: false
                transformOrigin: Item.Center

                Text {
                    id: bracketPulseText

                    anchors.centerIn: parent
                    font: viewport.font
                }

                SequentialAnimation {
                    id: bracketPulseAnimation

                    NumberAnimation {
                        target: bracketPulse
                        property: "scale"
                        from: 1.0
                        to: 1.5
                        duration: 128
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation {
                        target: bracketPulse
                        property: "scale"
                        to: 1.0
                        duration: 128
                        easing.type: Easing.InOutSine
                    }
                    onFinished: bracketPulse.visible = false
                }

                Connections {
                    target: viewport

                    function onAnimateCharacter(at: rect, text: string, foreground: color,
                                                background: color) {
                        bracketPulse.x = at.x
                        bracketPulse.y = at.y
                        bracketPulse.width = at.width
                        bracketPulse.height = at.height
                        bracketPulse.color = background
                        bracketPulseText.text = text
                        bracketPulseText.color = foreground
                        bracketPulse.visible = true
                        bracketPulseAnimation.restart()
                    }
                }
            }

            // The caret. A Rectangle because that is what it is; the viewport
            // draws text and says where the caret belongs, and an empty rect
            // is how it says the position is scrolled off screen.
            // One per caret. A single timer for all of them: carets blinking
            // out of phase with each other would be unreadable, and each
            // delegate owning one is how that happens.
            Item {
                id: carets

                property real blink: 1
                readonly property bool shown: viewport.activeFocus

                Repeater {
                    model: viewport.caretRectangles

                    delegate: Rectangle {
                        required property rect modelData

                        objectName: "caret"

                        x: modelData.x
                        y: modelData.y
                        width: modelData.width
                        height: modelData.height
                        visible: modelData.width > 0 && carets.shown
                        opacity: carets.blink
                        color: Tokens.textDefault
                    }
                }

                Timer {
                    running: carets.shown
                    repeat: true
                    // The platform's own blink rate, halved because one blink
                    // is two of these. Zero or less means do not blink at all.
                    interval: Application.styleHints.cursorFlashTime / 2
                    onTriggered: carets.blink = carets.blink > 0 ? 0 : 1
                }

                // Steady again whenever the carets move, so that typing does
                // not leave one invisible half the time.
                Connections {
                    target: viewport
                    function onCursorRectangleChanged(): void { carets.blink = 1 }
                }
            }
        }

        WheelHandler {
            target: null
            // A WheelHandler takes only an actual wheel unless it is told
            // otherwise, so without this a trackpad scrolls nothing at all.
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            // Both wheels: a trackpad has a sideways one, and tilting a mouse
            // wheel or holding Shift reports as one.
            orientation: Qt.Horizontal | Qt.Vertical
            onWheel: (event) => {
                // Ctrl and the wheel is a zoom rather than a scroll, and the
                // viewport is what knows whether the user allows it.
                if (event.modifiers & Qt.ControlModifier) {
                    viewport.zoomBy(event.angleDelta.y / 120)
                    return
                }
                // A trackpad says how far in pixels, which is what makes it
                // follow the fingers; a wheel says how far in eighths of a
                // degree, one notch being 120. How far a notch goes is the
                // reader's own setting, and the widget editor takes it from
                // here too - by way of a scroll bar whose step is one line.
                const notchLines = Application.styleHints.wheelScrollLines
                if (event.pixelDelta.y !== 0)
                    viewport.scrollY -= event.pixelDelta.y
                else if (event.angleDelta.y !== 0)
                    viewport.scrollY -= event.angleDelta.y / 120 * viewport.lineHeight * notchLines

                // Sideways too. A long line runs off the edge with wrapping
                // off, and this is the only way to follow it with the hands
                // rather than the caret. The widget editor's horizontal bar
                // steps by twenty pixels rather than by a line.
                if (event.pixelDelta.x !== 0)
                    viewport.scrollX -= event.pixelDelta.x
                else if (event.angleDelta.x !== 0)
                    viewport.scrollX -= event.angleDelta.x / 120 * 20 * notchLines
            }
        }

        // The document drawn small, between the text and the bar. It takes no
        // room at all when the settings do not ask for it, so the text is as
        // wide as it would be without one.
        MinimapView {
            id: minimap

            objectName: "minimap"

            viewport: viewport
            visible: wanted
            width: visible ? 100 : 0
            anchors.right: verticalScrollBar.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
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

        // Where the marks and the caret are, over the bar rather than in it:
        // a ScrollBar draws its own handle and this has to stay visible when
        // the handle passes under it.
        Item {
            objectName: "scrollBarHighlights"

            anchors.fill: verticalScrollBar
            visible: verticalScrollBar.visible
            z: 1

            Repeater {
                model: viewport.scrollBarHighlights

                delegate: Rectangle {
                    required property var modelData

                    x: 0
                    y: Math.round(modelData.position * parent.height)
                    width: parent.width
                    height: 2
                    color: modelData.color
                }
            }
        }

        ScrollBar {
            id: horizontalScrollBar

            objectName: "horizontalScrollBar"
            anchors.left: parent.left
            anchors.right: verticalScrollBar.left
            anchors.bottom: parent.bottom
            orientation: Qt.Horizontal
            // Only where a line runs past the edge, which with wrapping on is
            // never - the same rule the vertical one follows for a short file.
            policy: size < 1 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            size: viewport.contentWidth > 0
                  ? viewport.width / viewport.contentWidth
                  : 1
            position: viewport.contentWidth > 0
                      ? viewport.scrollX / viewport.contentWidth
                      : 0
            onPositionChanged: {
                if (pressed)
                    viewport.scrollX = position * viewport.contentWidth
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

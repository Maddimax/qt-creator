// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls

// What a pane's output looks like. The text itself is a document handed over
// from C++ - see QtcQuick::OutputView - so nothing here holds or formats it,
// and neither the zoom arithmetic nor what a link means lives here either.
Item {
    id: root

    // The font before the zoom. QtcQuick::OutputView reads this to learn the
    // size a zoom of 0 means, and writes effectiveFont.
    property font baseFont: Fonts.body1

    // The font to read the output in, base plus zoom. Written from C++.
    property font effectiveFont: root.baseFont

    property bool wheelZoomEnabled: true

    property bool wordWrapEnabled: false
    property color backgroundColor: Tokens.backgroundDefault

    // What the context menu asks for. The view does none of it: where to save
    // and what a scratch buffer is are decisions for whoever holds the output.
    // Entries a pane adds to the context menu, as QActions. Read for their
    // text and enabled state and triggered when chosen; what they mean is the
    // pane's business.
    property var extraActions: []

    // Set when those entries are the whole menu rather than an addition, which
    // is what the VCS pane does over one of its links.
    property bool extraActionsReplaceStandard: false

    // Emitted before the menu opens, with the point it was asked for, so a
    // pane can decide what to offer for whatever is under the cursor there.
    signal contextMenuAboutToShow(real x, real y)

    signal saveContentsRequested()
    signal copyContentsToScratchBufferRequested()
    signal clearRequested()

    // Where the pointer last was, so that a link click can say where it
    // happened. See the hover handler below.
    property real lastPointerX: 0
    property real lastPointerY: 0

    signal linkActivated(string href, real x, real y)
    // One notch of Ctrl+wheel, in points. What that comes to is C++'s
    // decision, because the widget output window has to agree with it.
    signal zoomRequested(real delta)

    Rectangle {
        anchors.fill: parent
        color: root.backgroundColor
    }

    // Named so a test can find it: a Menu is a popup and its items are not in
    // the page's item tree.
    Menu {
        id: contextMenu
        objectName: "outputContextMenu"

        MenuItem {
            objectName: "copy"
            visible: !root.extraActionsReplaceStandard
            text: qsTr("&Copy")
            enabled: area.selectedText.length > 0
            onTriggered: area.copy()
        }
        MenuItem {
            objectName: "selectAll"
            visible: !root.extraActionsReplaceStandard
            text: qsTr("Select &All")
            enabled: area.length > 0
            onTriggered: area.selectAll()
        }
        MenuSeparator { visible: !root.extraActionsReplaceStandard }
        MenuItem {
            objectName: "saveContents"
            visible: !root.extraActionsReplaceStandard
            text: qsTr("Save Contents...")
            enabled: area.length > 0
            onTriggered: root.saveContentsRequested()
        }
        MenuItem {
            objectName: "copyToScratchBuffer"
            visible: !root.extraActionsReplaceStandard
            text: qsTr("Copy Contents to Scratch Buffer")
            enabled: area.length > 0
            onTriggered: root.copyContentsToScratchBufferRequested()
        }
        MenuSeparator { visible: !root.extraActionsReplaceStandard }
        Instantiator {
            id: extraItems
            model: root.extraActions
            delegate: MenuItem {
                required property var modelData
                objectName: "extraAction"
                text: modelData.text
                enabled: modelData.enabled
                onTriggered: modelData.trigger()
            }
            onObjectAdded: (index, object) => contextMenu.addItem(object as MenuItem)
            onObjectRemoved: (index, object) => contextMenu.removeItem(object as MenuItem)
        }

        MenuItem {
            objectName: "clear"
            visible: !root.extraActionsReplaceStandard
            text: qsTr("Clear")
            enabled: area.length > 0
            onTriggered: root.clearRequested()
        }
    }

    // Whether the view is following the end of the output. It follows while
    // the reader is at the bottom and stops the moment they scroll up, or
    // reading back through a build would be impossible while it is running.
    readonly property Flickable flickable: scrollView.contentItem as Flickable

    QtObject {
        id: scrolling
        property bool following: true

        // Where the reader left the view. Kept because appending text moves
        // the text item's cursor to the end, and a text item inside a
        // flickable scrolls to its own cursor - so output arriving drags the
        // view to the bottom whatever this file decides.
        property real readerPosition: 0
    }

    // Readable so a test can say why the view did or did not follow.
    readonly property bool followingEnd: scrolling.following

    function scrollToBottom(): void {
        scrolling.following = true
        root.moveToEnd()
    }

    function stayWhereTheReaderIs(): void {
        if (root.flickable && !scrolling.following)
            root.flickable.contentY = scrolling.readerPosition
    }

    function moveToEnd(): void {
        if (!root.flickable || !scrolling.following)
            return
        root.flickable.contentY = Math.max(0, root.flickable.contentHeight
                                              - root.flickable.height)
    }

    // Only the reader moving the view stops it following. Watching every
    // change to the position instead does not work: a flickable adjusts its
    // own position as content arrives, and reading that as the reader
    // scrolling away stops the view following after the first line.
    Connections {
        target: root.flickable
        function onMovementEnded(): void {
            scrolling.following = root.flickable.atYEnd
            scrolling.readerPosition = root.flickable.contentY
        }
    }

    Connections {
        target: verticalScrollBar
        function onPressedChanged(): void {
            // A scroll bar drag moves the flickable directly and reports no
            // movement of its own, so it has to be asked about separately.
            if (!verticalScrollBar.pressed && root.flickable) {
                scrolling.following = root.flickable.atYEnd
                scrolling.readerPosition = root.flickable.contentY
            }
        }
    }

    Connections {
        target: area
        function onContentHeightChanged(): void {
            if (scrolling.following) {
                root.scrollToBottom()
            } else {
                // Now and again a turn later: the text item scrolls to its
                // cursor as part of the change, and putting the view back only
                // afterwards would show the reader a jump each time output
                // arrives.
                root.stayWhereTheReaderIs()
                Qt.callLater(root.stayWhereTheReaderIs)
            }
        }
    }

    ScrollView {
        id: scrollView

        anchors.fill: parent
        clip: true

        // Declared rather than left to the style, so that the view can be
        // told when the reader has finished dragging it.
        ScrollBar.vertical: ScrollBar { id: verticalScrollBar }

        TextArea {
            id: area
            objectName: "outputText"

            // Wrapping needs something to wrap against: inside a ScrollView the
            // text area is as wide as its longest line, and a wrap mode with
            // no width to obey does nothing at all.
            width: root.wordWrapEnabled ? scrollView.availableWidth : implicitWidth

            readOnly: true
            // Selectable, because copying a compiler error out of the pane is
            // half of what the pane is for.
            selectByMouse: true
            font: root.effectiveFont
            wrapMode: root.wordWrapEnabled ? TextArea.WrapAtWordBoundaryOrAnywhere
                                            : TextArea.NoWrap
            background: null
            padding: 0

            onLinkActivated: (link) => root.linkActivated(link, root.lastPointerX,
                                                          root.lastPointerY)

            HoverHandler {
                // Where the pointer last was. A link click reports no
                // position of its own, and a pane needs one to say which line
                // the link was on - the pointer is necessarily there.
                onPointChanged: {
                    root.lastPointerX = point.position.x
                    root.lastPointerY = point.position.y
                }
                cursorShape: area.hoveredLink.length > 0 ? Qt.PointingHandCursor
                                                         : Qt.IBeamCursor
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: (eventPoint) => {
                    // Asked before opening, not after: what a pane offers
                    // depends on what is under the point that was clicked.
                    root.contextMenuAboutToShow(eventPoint.position.x, eventPoint.position.y)
                    contextMenu.popup(eventPoint.position)
                }
            }

            WheelHandler {
                enabled: root.wheelZoomEnabled
                acceptedModifiers: Qt.ControlModifier
                onWheel: (event) => root.zoomRequested(event.angleDelta.y / 120)
            }
        }
    }

}

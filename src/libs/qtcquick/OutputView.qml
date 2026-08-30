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
    signal saveContentsRequested()
    signal copyContentsToScratchBufferRequested()
    signal clearRequested()

    signal linkActivated(string href)
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
            text: qsTr("&Copy")
            enabled: area.selectedText.length > 0
            onTriggered: area.copy()
        }
        MenuItem {
            objectName: "selectAll"
            text: qsTr("Select &All")
            enabled: area.length > 0
            onTriggered: area.selectAll()
        }
        MenuSeparator {}
        MenuItem {
            objectName: "saveContents"
            text: qsTr("Save Contents...")
            enabled: area.length > 0
            onTriggered: root.saveContentsRequested()
        }
        MenuItem {
            objectName: "copyToScratchBuffer"
            text: qsTr("Copy Contents to Scratch Buffer")
            enabled: area.length > 0
            onTriggered: root.copyContentsToScratchBufferRequested()
        }
        MenuSeparator {}
        MenuItem {
            objectName: "clear"
            text: qsTr("Clear")
            enabled: area.length > 0
            onTriggered: root.clearRequested()
        }
    }

    ScrollView {
        id: scrollView

        anchors.fill: parent
        clip: true

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

            onLinkActivated: (link) => root.linkActivated(link)

            HoverHandler {
                cursorShape: area.hoveredLink.length > 0 ? Qt.PointingHandCursor
                                                         : Qt.IBeamCursor
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: (eventPoint) => {
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

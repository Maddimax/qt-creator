// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// What an aspect offers to complete against, over the field being typed in.
// The widget line edit gets this from a QCompleter; QtQuick.Controls has
// nothing of the kind, so this is it.
//
// The field says what has been typed so far and takes back what was chosen;
// which part of the text that is - the whole field, or the word under the
// cursor - is the field's business, not this one's.
Popup {
    id: root

    // Everything on offer, and the part of the text to narrow it by.
    required property var completions
    required property string prefix

    // What the user settled on. The field replaces its prefix with this.
    signal accepted(string completion)

    // Nothing to offer, or nothing left that is not already typed in full.
    readonly property var matches: {
        if (root.prefix === "")
            return []
        const lower = root.prefix.toLowerCase()
        const found = []
        for (let i = 0; i < root.completions.length; ++i) {
            const candidate = root.completions[i]
            if (candidate.toLowerCase().startsWith(lower) && candidate !== root.prefix)
                found.push(candidate)
        }
        return found
    }

    // Opening with nothing to show would put an empty box over the text.
    function offer(): void {
        if (root.matches.length === 0) {
            root.close()
            return
        }
        list.currentIndex = 0
        root.open()
    }

    // Moving through the list and choosing from it happens while the field
    // still has the focus - a popup that took it would stop the typing.
    function moveDown(): void { list.incrementCurrentIndex() }
    function moveUp(): void { list.decrementCurrentIndex() }

    function acceptCurrent(): void {
        if (list.currentIndex >= 0 && list.currentIndex < root.matches.length)
            root.accepted(root.matches[list.currentIndex])
        root.close()
    }

    objectName: "completionPopup"
    padding: 1
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    // The list is what has been typed towards, so it follows the text.
    onMatchesChanged: {
        if (root.visible && root.matches.length === 0)
            root.close()
        else
            list.currentIndex = 0
    }

    implicitWidth: Math.max(Metrics.lineEditWidth, list.contentWidth + 2 * padding)
    implicitHeight: Math.min(Metrics.formListHeight, list.contentHeight + 2 * padding)

    contentItem: ListView {
        id: list

        objectName: "completionList"
        model: root.matches
        clip: true
        keyNavigationWraps: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            id: row

            required property int index
            required property string modelData

            width: list.width
            text: row.modelData
            highlighted: list.currentIndex === row.index
            onClicked: {
                list.currentIndex = row.index
                root.acceptCurrent()
            }
        }
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The row above the search results: what was searched for, how it is going,
// and - while replacing - what to replace it with. Every answer here is the
// controller's; see Core::Internal::SearchResultHeader for why none of it can
// live in the things that draw it.
ColumnLayout {
    id: root

    // Handed over before the source is set, by whichever front end hosts this.
    required property var controller

    objectName: "searchResultsRow"
    spacing: Spacing.GapVXxs

    RowLayout {
        Layout.fillWidth: true
        spacing: Spacing.GapHS

        Label {
            objectName: "searchDescription"
            visible: text !== ""
            text: root.controller.label
            color: Tokens.textMuted
            font: Fonts.body2
            ToolTip.text: root.controller.description
            ToolTip.visible: hovered && root.controller.description !== ""

            property bool hovered: descriptionHover.hovered
            HoverHandler { id: descriptionHover }
        }

        Label {
            objectName: "searchTerm"
            visible: text !== ""
            // Plain text: a search for "<b>" is a search for those three
            // characters, not an instruction to the label.
            textFormat: Text.PlainText
            text: root.controller.term
            color: Tokens.textDefault
            font: Fonts.body1
            elide: Text.ElideRight
            Layout.maximumWidth: root.width / 3
        }

        QtcButton {
            objectName: "cancelSearchButton"
            visible: root.controller.canCancel
            role: QtcButton.Role.SmallSecondary
            text: qsTr("Cancel")
            onClicked: root.controller.cancel()
        }

        QtcButton {
            objectName: "searchAgainButton"
            visible: root.controller.canSearchAgain
            role: QtcButton.Role.SmallSecondary
            text: qsTr("Search Again")
            ToolTip.text: qsTr("Repeat the search with same parameters.")
            ToolTip.visible: hovered
            onClicked: root.controller.searchAgain()
        }

        Item { Layout.fillWidth: true }

        Label {
            objectName: "matchesFoundLabel"
            text: root.controller.matchesFound
            color: Tokens.textMuted
            font: Fonts.body2
        }
    }

    RowLayout {
        objectName: "replaceRow"
        Layout.fillWidth: true
        visible: root.controller.showingReplaceUi
        spacing: Spacing.GapHS

        Label {
            text: qsTr("Replace with:")
            color: Tokens.textMuted
            font: Fonts.body2
        }

        TextField {
            objectName: "replaceField"
            Layout.minimumWidth: Metrics.lineEditWidth
            text: root.controller.textToReplace
            font: Fonts.body1
            // Bound both ways deliberately: the controller is where the text
            // lives, and a replace reads it from there rather than from here.
            onTextEdited: root.controller.textToReplace = text
            onAccepted: root.controller.replace()
        }

        CheckBox {
            objectName: "preserveCaseCheck"
            visible: root.controller.supportsPreserveCase
            text: qsTr("Preserve case")
            checked: root.controller.preserveCaseChecked
            onToggled: root.controller.preserveCaseChecked = checked
        }

        Label {
            objectName: "replaceNote"
            visible: text !== ""
            text: root.controller.additionalNote
            color: Tokens.textMuted
            font: Fonts.body2
        }

        CheckBox {
            objectName: "additionalOptionCheck"
            visible: root.controller.hasAdditionalOption
            text: root.controller.additionalOptionLabel
            checked: root.controller.additionalOptionChecked
            ToolTip.text: root.controller.additionalOptionToolTip
            ToolTip.visible: hovered && root.controller.additionalOptionToolTip !== ""
            onToggled: root.controller.additionalOptionChecked = checked
        }

        Item { Layout.fillWidth: true }

        QtcButton {
            objectName: "replaceButton"
            enabled: root.controller.canReplace
            role: QtcButton.Role.SmallPrimary
            text: qsTr("Replace")
            ToolTip.text: qsTr("Replace all occurrences.")
            ToolTip.visible: hovered
            onClicked: root.controller.replace()
        }
    }
}

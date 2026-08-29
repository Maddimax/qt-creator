// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// Mirrors Utils::QtcSearchBox: a QtcLineEdit with a trailing search icon shown
// while it is empty.
//
// Utils::QtcSearchBox draws Core's ":/core/images/search.png", a resource of
// the Core plugin that this module cannot reach. Utils::Icons::MAGNIFIER
// (":/utils/images/magnifier.png") is the equivalent icon already available
// through the shared icon provider, so it stands in here.
//
// The clear button is not on Utils::QtcSearchBox but is on every search field
// this replaces - the widget file dialog builds its own from a FancyLineEdit
// with a magnifier on one side and EDIT_CLEAR on the other. Without it a
// search can only be undone with the keyboard. It takes the same corner as
// the magnifier: one is for an empty field and the other never is.
QtcLineEdit {
    id: root

    rightContentPadding: Spacing.PaddingHM + Metrics.listRowIconSize + Spacing.GapHXs

    Accessible.searchEdit: true

    Image {
        id: icon

        objectName: "searchIcon"
        visible: root.text.length === 0
        source: "image://qtcreator/utils/images/magnifier.png?color=Token_Text_Muted"
        // Without a size of its own an Image draws what the provider hands
        // back, which is the icon at twice this.
        sourceSize.width: Metrics.listRowIconSize
        sourceSize.height: Metrics.listRowIconSize
        fillMode: Image.PreserveAspectFit
        opacity: root.enabled ? 1.0 : Metrics.disabledIconOpacity
        anchors.right: parent.right
        anchors.rightMargin: Spacing.PaddingHM
        anchors.verticalCenter: parent.verticalCenter
    }

    Image {
        id: clear

        objectName: "clearButton"
        visible: root.text.length > 0 && root.enabled
        source: "image://qtcreator/utils/images/editclear.png?color=PanelTextColorMid"
        sourceSize.width: Metrics.listRowIconSize
        sourceSize.height: Metrics.listRowIconSize
        fillMode: Image.PreserveAspectFit
        anchors.right: parent.right
        anchors.rightMargin: Spacing.PaddingHM
        anchors.verticalCenter: parent.verticalCenter

        ToolTip.text: qsTr("Clear")
        ToolTip.visible: hover.hovered

        HoverHandler { id: hover; cursorShape: Qt.ArrowCursor }
        TapHandler { onTapped: root.text = "" }
    }
}

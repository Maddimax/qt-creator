// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Mirrors Utils::QtcLineEdit's card background and typography. QtQuick's
// TextInput has no placeholder support of its own, so it is added here as a
// plain Text shown while empty, the same condition QtcSearchBox uses for its
// leading icon.
//
// A FocusScope, not a plain Item: the actual keyboard focus target is the
// inner TextInput, and this lets a caller focus the whole component with
// "focus: true" or forceActiveFocus(), same as any other focusable QML type.
FocusScope {
    id: root

    property alias text: input.text
    property string placeholderText: ""
    property alias readOnly: input.readOnly
    property alias validator: input.validator
    property alias maximumLength: input.maximumLength

    // Selecting what is there, for a caller that puts the reader in the field
    // to replace its contents. The field itself is not exposed, so this has
    // to be forwarded.
    function selectAll(): void {
        input.selectAll()
    }
    property string accessibleName: root.placeholderText

    // Overridable by a subclass (QtcSearchBox) that draws its own icon
    // over part of the card and needs the text to stay clear of it.
    property real leftContentPadding: Spacing.PaddingHM
    property real rightContentPadding: Spacing.PaddingHM

    readonly property bool hovered: hoverHandler.hovered

    signal accepted()
    signal editingFinished()

    implicitWidth: Metrics.lineEditWidth
    implicitHeight: Fonts.body2LineHeight + 2 * Spacing.PaddingVS

    Accessible.role: Accessible.EditableText
    Accessible.name: root.accessibleName
    Accessible.editable: true
    Accessible.readOnly: root.readOnly

    Rectangle {
        anchors.fill: parent
        radius: Spacing.RadiusS
        color: (root.hovered && root.enabled) ? Tokens.foregroundSubtle : Tokens.backgroundMuted
        border.width: 1
        border.color: !root.enabled ? Tokens.foregroundSubtle
                    : root.activeFocus ? Tokens.strokeStrong
                    : root.hovered ? Tokens.strokeMuted : Tokens.strokeSubtle
    }

    Text {
        text: root.placeholderText
        visible: input.text.length === 0
        font: Fonts.body2
        color: root.enabled ? Tokens.textMuted : Tokens.textSubtle
        elide: Text.ElideRight
        anchors.left: parent.left
        anchors.leftMargin: root.leftContentPadding
        anchors.right: parent.right
        anchors.rightMargin: root.rightContentPadding
        anchors.verticalCenter: parent.verticalCenter
    }

    TextInput {
        id: input
        anchors.left: parent.left
        anchors.leftMargin: root.leftContentPadding
        anchors.right: parent.right
        anchors.rightMargin: root.rightContentPadding
        anchors.verticalCenter: parent.verticalCenter
        clip: true
        enabled: root.enabled
        selectByMouse: true
        activeFocusOnTab: true
        focus: true
        font: Fonts.body2
        color: root.enabled ? Tokens.textDefault : Tokens.textSubtle
        selectionColor: Tokens.accentDefault
        selectedTextColor: Tokens.textOnAccent

        onAccepted: root.accepted()
        onEditingFinished: root.editingFinished()
    }

    HoverHandler {
        id: hoverHandler
        enabled: root.enabled
    }
}

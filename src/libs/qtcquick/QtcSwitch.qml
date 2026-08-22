// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Mirrors Utils::QtcSwitch: track, on/off glyph and knob, with the switch
// fixed to the right of its label regardless of layout direction (the
// widget forces Qt::RightToLeft for the same reason).
Item {
    id: root

    property string text: ""
    property bool checked: false

    readonly property bool hovered: hoverHandler.hovered
    readonly property bool pressed: tapHandler.pressed
    readonly property bool checkedEnabled: root.checked && root.enabled

    signal clicked()
    signal toggled()

    function activate(): void {
        if (!root.enabled)
            return
        root.checked = !root.checked
        root.toggled()
        root.clicked()
    }

    implicitWidth: Metrics.switchTrackWidth + Spacing.GapHM + label.implicitWidth
    implicitHeight: Math.max(Spacing.PrimitiveXl, Spacing.PaddingVS * 2 + Fonts.labelMediumLineHeight)

    activeFocusOnTab: root.enabled

    Accessible.role: Accessible.Switch
    Accessible.name: root.text
    Accessible.checkable: true
    Accessible.checked: root.checked
    Accessible.onPressAction: root.activate()
    Accessible.onToggleAction: root.activate()

    Text {
        id: label
        text: root.text
        visible: root.text.length > 0
        font: Fonts.labelMedium
        color: root.enabled ? Tokens.textDefault : Tokens.textSubtle
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: track.left
        anchors.rightMargin: Spacing.GapHM
    }

    Item {
        id: track
        width: Metrics.switchTrackWidth
        height: Spacing.PrimitiveXl
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter

        Rectangle {
            id: trackBackground
            anchors.fill: parent
            radius: height / 2
            color: root.checkedEnabled ? (root.hovered ? Tokens.accentSubtle : Tokens.accentDefault)
                                       : Tokens.foregroundSubtle
            border.width: root.checkedEnabled ? 0 : 1
            border.color: root.hovered ? Tokens.strokeMuted : Tokens.strokeSubtle

            Rectangle {
                // The "on" glyph: a short vertical bar.
                visible: root.checked
                width: 1
                height: Metrics.switchMarkSize
                border.width: 0
                x: Spacing.PrimitiveM
                anchors.verticalCenter: parent.verticalCenter
                color: root.enabled ? Tokens.textOnAccent : Tokens.textSubtle
            }

            Rectangle {
                // The "off" glyph: a small ring, one pixel off the track's edge
                // (matching the widget's own literal offset).
                visible: !root.checked
                width: Metrics.switchMarkSize
                height: Metrics.switchMarkSize
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: root.enabled ? Tokens.textMuted : Tokens.textSubtle
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: parent.height / 2 - width / 2 - 1
            }
        }

        Rectangle {
            id: knob
            readonly property int inset: root.checkedEnabled ? Metrics.switchKnobInsetChecked
                                                              : Metrics.switchKnobInsetUnchecked

            y: inset
            x: root.checked ? track.width - width - inset : inset
            height: track.height - inset * 2
            width: root.pressed ? (root.checkedEnabled ? Metrics.switchKnobPressedWidthChecked
                                                        : Metrics.switchKnobPressedWidthUnchecked)
                                : height
            radius: height / 2
            color: root.enabled ? Tokens.basicWhite : Tokens.foregroundDefault
            border.width: root.checkedEnabled ? 0 : 1
            border.color: Tokens.strokeSubtle

            Behavior on x {
                NumberAnimation { duration: 80; easing.type: Easing.OutCubic }
            }
        }
    }

    HoverHandler {
        id: hoverHandler
        enabled: root.enabled
    }

    TapHandler {
        id: tapHandler
        enabled: root.enabled
        onTapped: root.activate()
    }

    Keys.onPressed: (event) => {
        if (!root.enabled)
            return
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.activate()
            event.accepted = true
        }
    }
}

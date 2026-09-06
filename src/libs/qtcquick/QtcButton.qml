// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Mirrors Utils::QtcButton (src/libs/utils/qtdesignwidgets.h/.cpp): the same
// 15 role names, paddings and colours, minus mnemonics (no QtQuick
// equivalent) and the press-state size hint (layout, not painting, decides
// that here).
Item {
    id: root

    enum Role {
        LargePrimary,
        LargeSecondary,
        LargeTertiary,
        LargeGhost,
        MediumPrimary,
        MediumSecondary,
        MediumTertiary,
        MediumGhost,
        SmallPrimary,
        SmallSecondary,
        SmallTertiary,
        SmallGhost,
        SmallList,
        SmallLink,
        Tag
    }

    property string text: ""
    property int role: QtcButton.Role.MediumPrimary
    property string iconSource: ""
    // What the glyph is drawn at. The images are provided at twice this and
    // an Image with no size of its own draws them at that, which is what made
    // a row of toolbar buttons twice the height of the field beside them.
    property int iconSize: 16
    property bool checked: false

    // The role decides how a checked button looks; whether it is checkable
    // at all is the caller's, so a button standing for a checkable action
    // can say so without having to become a SmallList to be allowed to.
    property bool checkable: root.role === QtcButton.Role.SmallList
    readonly property bool hovered: hoverHandler.hovered
    readonly property bool pressed: tapHandler.pressed

    signal clicked()

    function activate(): void {
        if (!root.enabled)
            return
        if (root.checkable)
            root.checked = !root.checked
        root.clicked()
    }

    readonly property bool isLarge: root.role === QtcButton.Role.LargePrimary
                                    || root.role === QtcButton.Role.LargeSecondary
                                    || root.role === QtcButton.Role.LargeTertiary
                                    || root.role === QtcButton.Role.LargeGhost
    readonly property bool isSmallPlain: root.role === QtcButton.Role.SmallPrimary
                                         || root.role === QtcButton.Role.SmallSecondary
                                         || root.role === QtcButton.Role.SmallTertiary
                                         || root.role === QtcButton.Role.SmallGhost
    readonly property bool isPrimary: root.role === QtcButton.Role.LargePrimary
                                      || root.role === QtcButton.Role.MediumPrimary
                                      || root.role === QtcButton.Role.SmallPrimary
    readonly property bool isSecondary: root.role === QtcButton.Role.LargeSecondary
                                        || root.role === QtcButton.Role.MediumSecondary
                                        || root.role === QtcButton.Role.SmallSecondary
    readonly property bool isTertiary: root.role === QtcButton.Role.LargeTertiary
                                       || root.role === QtcButton.Role.MediumTertiary
                                       || root.role === QtcButton.Role.SmallTertiary
    readonly property bool isGhost: root.role === QtcButton.Role.LargeGhost
                                    || root.role === QtcButton.Role.MediumGhost
                                    || root.role === QtcButton.Role.SmallGhost
    readonly property bool isList: root.role === QtcButton.Role.SmallList
    readonly property bool isLink: root.role === QtcButton.Role.SmallLink
    readonly property bool isTag: root.role === QtcButton.Role.Tag

    // hovered || pressed || checked: what SmallList and Tag fill on.
    readonly property bool filled: root.hovered || root.pressed || root.checked

    readonly property int hPadding: root.isLarge ? Spacing.PaddingHXl
                                    : root.isSmallPlain ? Spacing.PaddingHM
                                    : root.isTag ? Spacing.PaddingHM
                                    : Spacing.PaddingHL
    readonly property int vPadding: root.isLarge ? Spacing.PaddingVL
                                    : root.isSmallPlain ? Spacing.PaddingVS
                                    : root.isTag ? Spacing.PaddingVXs
                                    : Spacing.PaddingVM
    readonly property int iconLabelGap: root.isLarge ? Spacing.GapHM : Spacing.GapHXs
    readonly property bool centerLabel: root.isPrimary || root.isSecondary || root.isTertiary
                                        || root.isGhost

    readonly property font labelFont: {
        if (root.isLarge)
            return Fonts.h5
        if (root.role === QtcButton.Role.MediumPrimary || root.role === QtcButton.Role.MediumSecondary
            || root.role === QtcButton.Role.MediumTertiary || root.role === QtcButton.Role.MediumGhost)
            return Fonts.buttonMedium
        if (root.isSmallPlain)
            return Fonts.buttonSmall
        if (root.isList || root.isLink)
            return Fonts.iconStandard
        return Fonts.labelMedium // Tag
    }
    // Weight and slant on top of the role's font, for a button standing for
    // something whose label *is* the emphasis - Markdown's italic "i" and bold
    // "b". Not the whole font: the size and the family are the type scale's,
    // and a caller able to set those would step outside it.
    property bool labelBold: false
    property bool labelItalic: false

    // The role's font untouched unless something asked for emphasis: reading a
    // font property gives a reference to it, so mutating a local copy of it
    // writes back through to a readonly property and is dropped. Built from
    // parts instead, and only where it has to be.
    readonly property font emphasisedLabelFont: {
        if (!root.labelBold && !root.labelItalic)
            return root.labelFont
        let spec = {
            family: root.labelFont.family,
            weight: root.labelBold ? Font.Bold : root.labelFont.weight,
            italic: root.labelItalic || root.labelFont.italic,
            letterSpacing: root.labelFont.letterSpacing
        }
        // Point size where the font has one. QML reports a pixelSize for a
        // point-sized font too - it computes one - so asking which is set has
        // to be asked of pointSize. Carrying the wrong one over turns a point
        // size into a pixel size: the same number, a different size on a
        // high-DPI screen.
        if (root.labelFont.pointSize > 0)
            spec.pointSize = root.labelFont.pointSize
        else
            spec.pixelSize = root.labelFont.pixelSize
        return Qt.font(spec)
    }

    readonly property int labelLineHeight: {
        if (root.isLarge)
            return Fonts.h5LineHeight
        if (root.role === QtcButton.Role.MediumPrimary || root.role === QtcButton.Role.MediumSecondary
            || root.role === QtcButton.Role.MediumTertiary || root.role === QtcButton.Role.MediumGhost)
            return Fonts.buttonMediumLineHeight
        if (root.isSmallPlain)
            return Fonts.buttonSmallLineHeight
        if (root.isList || root.isLink)
            return Fonts.iconStandardLineHeight
        return Fonts.labelMediumLineHeight // Tag
    }

    readonly property color labelColor: {
        if (!root.enabled)
            return Tokens.textSubtle
        if (root.isPrimary)
            return Tokens.textOnAccent
        if (root.isLink)
            return root.hovered ? Tokens.textAccent : Tokens.textDefault
        if (root.isTag)
            return root.filled ? Tokens.textDefault : Tokens.textMuted
        return Tokens.textDefault
    }

    readonly property color backgroundColor: {
        if (root.isPrimary) {
            if (!root.enabled)
                return Tokens.foregroundSubtle
            return root.pressed ? Tokens.accentSubtle
                 : root.hovered ? Tokens.accentMuted : Tokens.accentDefault
        }
        if (root.isSecondary)
            return root.pressed ? Tokens.foregroundSubtle : "transparent"
        if (root.isTertiary || (root.isGhost && (root.hovered || root.pressed))) {
            if (!root.enabled)
                return Tokens.foregroundSubtle
            return root.pressed ? Tokens.foregroundDefault
                 : root.hovered ? Tokens.foregroundMuted : Tokens.foregroundSubtle
        }
        if (root.isGhost)
            return "transparent"
        if (root.isList)
            return root.filled ? (root.checked ? Tokens.foregroundMuted : Tokens.foregroundSubtle)
                                : "transparent"
        if (root.isLink)
            return "transparent"
        return root.filled ? Tokens.foregroundSubtle : "transparent" // Tag
    }

    readonly property real borderWidth: {
        if (root.isSecondary)
            return root.hovered ? 2 : 1
        if (root.isTag)
            return root.hovered ? 0 : 1
        return 0
    }

    readonly property color borderColor: {
        if (root.isSecondary)
            return root.enabled ? Tokens.strokeStrong : Tokens.strokeSubtle
        if (root.isTag)
            return Tokens.strokeSubtle
        return "transparent"
    }

    readonly property bool hasIcon: root.iconSource.length > 0
    readonly property real leftContentPadding: root.hPadding
                                                + (root.hasIcon ? icon.width + root.iconLabelGap : 0)

    implicitWidth: root.leftContentPadding + label.implicitWidth + root.hPadding
    implicitHeight: root.vPadding * 2 + root.labelLineHeight

    Accessible.role: root.checkable ? Accessible.CheckBox : Accessible.Button
    Accessible.name: root.text
    Accessible.checkable: root.checkable
    Accessible.checked: root.checked
    Accessible.onPressAction: root.activate()

    activeFocusOnTab: root.enabled

    Rectangle {
        anchors.fill: parent
        radius: Spacing.RadiusS
        color: root.backgroundColor
        border.width: root.borderWidth
        border.color: root.borderColor
    }

    Image {
        id: icon
        visible: root.hasIcon
        source: root.iconSource
        sourceSize.width: root.iconSize
        sourceSize.height: root.iconSize
        fillMode: Image.PreserveAspectFit
        anchors.left: parent.left
        anchors.leftMargin: root.hPadding
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        id: label
        text: root.text
        font: root.emphasisedLabelFont
        color: root.labelColor
        elide: Text.ElideRight
        horizontalAlignment: root.centerLabel ? Text.AlignHCenter : Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: root.leftContentPadding
        anchors.right: parent.right
        anchors.rightMargin: root.hPadding
    }

    HoverHandler {
        id: hoverHandler
        enabled: root.enabled
        cursorShape: root.isLink ? Qt.PointingHandCursor : Qt.ArrowCursor
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

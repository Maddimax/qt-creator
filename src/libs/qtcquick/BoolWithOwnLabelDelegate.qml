// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A check box whose text is a label of its own, next to the box rather than on
// it. A CheckBox draws its own text and draws it plain, so a setting whose
// label is more than text - "Use <a href=...>global settings</a>", which takes
// you to the page those settings come from - needs a label that can be asked
// about what was clicked.
RowLayout {
    id: root

    required property Aspect aspect
    // plainLabelText, as every delegate uses: it strips the mnemonic "&" and
    // leaves markup alone, which is the whole point here.
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    // Named as the plain delegate names them, so a test or a hand-written page
    // can read either without knowing which one it got.
    property alias checked: box.checked
    property alias text: root.labelText

    visible: aspectVisible
    enabled: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    CheckBox {
        id: box

        checked: (root.aspect?.value ?? false) === true
        ToolTip.text: root.toolTip
        ToolTip.visible: hovered && root.toolTip !== ""

        onToggled: if (root.aspect) root.aspect.value = checked
    }

    Label {
        text: root.labelText
        textFormat: Text.StyledText
        elide: Text.ElideRight
        Layout.fillWidth: true

        onLinkActivated: (link) => root.aspect?.activateLink(link)

        // A link is only a link if it looks clickable.
        HoverHandler {
            enabled: parent.hoveredLink !== ""
            cursorShape: Qt.PointingHandCursor
        }
    }
}

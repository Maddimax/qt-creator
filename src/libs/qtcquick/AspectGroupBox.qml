// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A group whose title is optionally the check box of a BoolAspect, which greys
// out the contents when unchecked. Qt Quick's GroupBox has no checkable
// property, unlike QGroupBox, so the check box goes in the label.
GroupBox {
    id: root

    // When set, the group is checkable and this aspect is what it checks.
    property Aspect checkAspect: null

    // What the group as a whole is for. A QGroupBox carried one and the layouts
    // these forms replaced set it; shown from the title, which is the one part
    // of a group that is not some delegate with a tool tip of its own.
    property string toolTip: ""

    readonly property bool checked: !checkAspect || checkAspect.value === true

    default property alias groupContent: contents.data

    Layout.fillWidth: true

    // A group that checks nothing still has a title to show, so the label is
    // the check box only when there is something to check.
    //
    // No width: the style's own label takes availableWidth so that it can
    // elide, but a Loader's implicit width is its item's, and the group's width
    // is worked out from its label's - so binding the Loader's width to the
    // group's closes a loop, on every page with a group on it. The label is as
    // wide as its text instead, which is what a QGroupBox title was too.
    label: Loader {
        id: labelLoader

        // Named so that a test can ask what the title says and what it
        // explains; a group's tool tip is otherwise only visible by hovering.
        objectName: "groupTitle"

        x: root.leftPadding
        sourceComponent: root.checkAspect ? checkLabel : titleLabel

        HoverHandler { id: titleHover }

        ToolTip.text: root.toolTip
        ToolTip.visible: titleHover.hovered && root.toolTip !== ""

        Component {
            id: checkLabel

            CheckBox {
                text: root.title
                checked: root.checked
                enabled: root.checkAspect?.enabled ?? true
                onToggled: if (root.checkAspect) root.checkAspect.value = checked
            }
        }

        Component {
            id: titleLabel

            Label {
                text: root.title
                font: Fonts.h6
            }
        }
    }

    // A GroupBox does not lay its children out - they keep their implicit size,
    // so a row of delegates stayed as narrow as its labels and a paragraph of
    // text ran off the page. Stack them and let each fill the width, which is
    // what the Column inside the widget Group did.
    contentItem: ColumnLayout {
        id: contents

        spacing: Spacing.GapVS
        enabled: root.checked

        onChildrenChanged: {
            for (let i = 0; i < children.length; ++i)
                children[i].Layout.fillWidth = true
        }
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The root of a hand-written settings page. Its children are laid out in a
// column; reach the container's aspects as aspects.<qmlName>.
ScrollView {
    id: root

    default property alias content: column.data

    // A NamedAspects for the page's container, set from C++.
    required property var aspects

    // Whether something on the page wants the height that is left over - a
    // table, a code preview. A page that says nothing is held at the top
    // instead of having its rows spread down the viewport, which is what a
    // column of plain settings did.
    property bool contentFillsHeight: false

    contentWidth: availableWidth

    ColumnLayout {
        id: column

        width: root.availableWidth
        // At least the viewport, so that a child asking to fill the height has
        // something to fill; taller when the content needs it.
        height: Math.max(implicitHeight, root.availableHeight)
        spacing: Spacing.GapVS

        Item {
            id: filler

            Layout.fillHeight: !root.contentFillsHeight
        }
    }

    // Last, whatever order the page's own children were given in: they are
    // appended to the same list, and a filler in front of them would push the
    // page down rather than hold it up. Re-parenting moves an item to the end,
    // but assigning the parent it already has does not, so it is taken out
    // first.
    Component.onCompleted: {
        filler.parent = null
        filler.parent = column
    }
}

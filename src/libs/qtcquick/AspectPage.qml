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

    contentWidth: availableWidth

    ColumnLayout {
        id: column

        width: root.availableWidth
        // At least the viewport, so that a child asking to fill the height has
        // something to fill; taller when the content needs it.
        height: Math.max(implicitHeight, root.availableHeight)
        spacing: Spacing.GapVS
    }
}

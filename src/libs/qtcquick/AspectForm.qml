// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

ScrollView {
    id: root

    // An AspectContainerModel, set from C++.
    required property var model

    contentWidth: availableWidth

    ColumnLayout {
        width: root.availableWidth
        // At least the viewport, so that a child asking to fill the height has
        // something to fill; taller when the content needs it.
        height: Math.max(implicitHeight, root.availableHeight)
        spacing: Spacing.GapVS

        AspectItems {
            Layout.fillWidth: true
            model: root.model
        }

        Item { Layout.fillHeight: true }
    }
}

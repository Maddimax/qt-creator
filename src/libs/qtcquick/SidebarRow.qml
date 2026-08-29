// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// One entry in the file dialog's sidebar: what it is, then what it is called.
// The style's ItemDelegate draws only text, and every section of the sidebar
// wants the same row.
RowLayout {
    id: root

    property alias iconSource: image.source
    property alias text: label.text

    spacing: Spacing.GapHS

    Image {
        id: image

        sourceSize.width: Metrics.listRowIconSize
        sourceSize.height: Metrics.listRowIconSize
        fillMode: Image.PreserveAspectFit
        Layout.preferredWidth: Metrics.listRowIconSize
        Layout.preferredHeight: Metrics.listRowIconSize
    }

    Label {
        id: label

        elide: Text.ElideMiddle
        Layout.fillWidth: true
    }
}

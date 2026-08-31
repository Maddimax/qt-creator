// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The button that opens the crop and trim dialog, with a warning beside it
// where what it would produce cannot be encoded.
AspectPage {
    id: root

    RowLayout {
        spacing: Spacing.GapHXs
        Layout.fillWidth: true

        ButtonDelegate { aspect: root.aspects.CropAndTrim }
        TextDisplayDelegate { aspect: root.aspects.Warning }
        Item { Layout.fillWidth: true }
    }
}

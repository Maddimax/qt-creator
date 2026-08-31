// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.ScreenRecorder

// The frame with the crop rectangle over it, and the rectangle again as four
// numbers. The scene scrolls: a frame is its own size, whatever the dialog is.
AspectPage {
    id: root

    contentFillsHeight: true

    ScrollView {
        clip: true
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Metrics.formListHeight

        CropScene {
            objectName: "cropScene"
            aspect: root.aspects.Scene
        }
    }

    RowLayout {
        spacing: Spacing.GapHXs
        Layout.fillWidth: true

        IntegerDelegate { aspect: root.aspects.X; compact: true }
        IntegerDelegate { aspect: root.aspects.Y; compact: true }
        IntegerDelegate { aspect: root.aspects.Width; compact: true }
        IntegerDelegate { aspect: root.aspects.Height; compact: true }

        ButtonDelegate { aspect: root.aspects.Reset }

        Item { Layout.fillWidth: true }

        ButtonDelegate { aspect: root.aspects.SaveImage }
        ButtonDelegate { aspect: root.aspects.CopyImage }
    }
}

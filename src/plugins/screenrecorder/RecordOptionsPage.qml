// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.ScreenRecorder

// Which display to record, how much of it, and how fast. The area is picked on
// a thumbnail of the display, which is the same scene the crop dialog uses.
AspectPage {
    id: root

    SelectionDelegate { aspect: root.aspects.Screen }

    AspectGroupBox {
        title: qsTr("Recorded screen area:")
        Layout.fillWidth: true

        CropScene {
            objectName: "recordCropScene"
            aspect: root.aspects.Scene
            Layout.alignment: Qt.AlignHCenter
        }

        RowLayout {
            spacing: Spacing.GapHXs
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }
            TextDisplayDelegate { aspect: root.aspects.CropRect }
            ButtonDelegate { aspect: root.aspects.Reset }
        }
    }

    IntegerDelegate { aspect: root.aspects.FrameRate }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.ScreenRecorder

// Where the reader is in the clip, and which part of it is kept.
AspectPage {
    id: root

    RowLayout {
        spacing: Spacing.GapHXs
        Layout.fillWidth: true

        TrimSlider {
            objectName: "trimSlider"
            aspect: root.aspects.Slider
            Layout.fillWidth: true
        }

        TextDisplayDelegate { aspect: root.aspects.CurrentTime }
        QtcLabel { text: "/" }
        TextDisplayDelegate { aspect: root.aspects.ClipDuration }
    }

    AspectGroupBox {
        title: qsTr("Trimming")
        Layout.fillWidth: true

        RowLayout {
            spacing: Spacing.GapHXs
            Layout.fillWidth: true

            ButtonDelegate { aspect: root.aspects.SetStart }
            TextDisplayDelegate { aspect: root.aspects.StartTime }

            Item { Layout.preferredWidth: Spacing.GapHL }

            ButtonDelegate { aspect: root.aspects.SetEnd }
            TextDisplayDelegate { aspect: root.aspects.EndTime }

            Item { Layout.fillWidth: true }

            QtcLabel { text: qsTr("Range:") }
            TextDisplayDelegate { aspect: root.aspects.RangeTime }
            ButtonDelegate { aspect: root.aspects.ResetTrim }
        }
    }
}

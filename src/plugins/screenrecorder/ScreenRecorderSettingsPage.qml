// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("FFmpeg Installation")

        ColumnLayout {
            StringDelegate { aspect: root.aspects.FFmpegTool }
            StringDelegate { aspect: root.aspects.FFprobeTool }
            TextDisplayDelegate { aspect: root.aspects.FFmpegWebsite }
        }
    }

    AspectGroupBox {
        title: qsTr("Record Settings")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.CaptureCursor }
            BoolDelegate { aspect: root.aspects.CaptureMouseClicks }
            SelectionDelegate { aspect: root.aspects.ScreenCaptureType }

            RowLayout {
                BoolDelegate { aspect: root.aspects.EnableFileSizeLimit }
                IntegerDelegate { aspect: root.aspects.FileSizeLimit; compact: true }
            }

            RowLayout {
                BoolDelegate { aspect: root.aspects.EnableRealTimeBuffer }
                IntegerDelegate { aspect: root.aspects.RealTimeBufferSize; compact: true }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Export Settings")

        BoolDelegate { aspect: root.aspects.AnimatedImagesAsEndlessLoop }
    }

    BoolDelegate { aspect: root.aspects.LogFFMpegCommandLine }
}

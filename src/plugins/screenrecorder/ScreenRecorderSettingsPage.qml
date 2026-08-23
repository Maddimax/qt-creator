// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    AspectGroupBox {
        title: qsTr("FFmpeg Installation")

        ColumnLayout {
            StringDelegate { aspect: aspects.FFmpegTool }
            StringDelegate { aspect: aspects.FFprobeTool }
            TextDisplayDelegate { aspect: aspects.FFmpegWebsite }
        }
    }

    AspectGroupBox {
        title: qsTr("Record Settings")

        ColumnLayout {
            BoolDelegate { aspect: aspects.CaptureCursor }
            BoolDelegate { aspect: aspects.CaptureMouseClicks }
            SelectionDelegate { aspect: aspects.ScreenCaptureType }

            RowLayout {
                BoolDelegate { aspect: aspects.EnableFileSizeLimit }
                IntegerDelegate { aspect: aspects.FileSizeLimit }
            }

            RowLayout {
                BoolDelegate { aspect: aspects.EnableRealTimeBuffer }
                IntegerDelegate { aspect: aspects.RealTimeBufferSize }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Export Settings")

        BoolDelegate { aspect: aspects.AnimatedImagesAsEndlessLoop }
    }

    BoolDelegate { aspect: aspects.LogFFMpegCommandLine }
}

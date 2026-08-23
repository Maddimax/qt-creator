// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    TextDisplayDelegate { aspect: aspects.EngineNote }

    AspectGroupBox {
        title: qsTr("Syntax Highlight Definition Files")

        ColumnLayout {
            RowLayout {
                ButtonDelegate { aspect: aspects.DownloadDefinitions }
                TextDisplayDelegate { aspect: aspects.UpdateStatus }
            }

            RowLayout {
                TextDisplayDelegate { aspect: aspects.UserFilesLabel }
                StringDelegate { aspect: aspects.UserDefinitionFilesPath }
                ButtonDelegate { aspect: aspects.ReloadDefinitions }
            }

            ButtonDelegate { aspect: aspects.ResetRememberedDefinitions }
        }
    }

    StringListDelegate { aspect: aspects.skipUpdateCheckForFilesPatterns }
    StringListDelegate { aspect: aspects.skipFilesPatterns }
}

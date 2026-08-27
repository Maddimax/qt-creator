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

    TextDisplayDelegate { aspect: root.aspects.EngineNote }

    AspectGroupBox {
        title: qsTr("Syntax Highlight Definition Files")

        ColumnLayout {
            RowLayout {
                ButtonDelegate { aspect: root.aspects.DownloadDefinitions }
                TextDisplayDelegate { aspect: root.aspects.UpdateStatus }
            }

            RowLayout {
                TextDisplayDelegate { aspect: root.aspects.UserFilesLabel }
                StringDelegate { aspect: root.aspects.UserDefinitionFilesPath }
                ButtonDelegate { aspect: root.aspects.ReloadDefinitions }
            }

            ButtonDelegate { aspect: root.aspects.ResetRememberedDefinitions }
        }
    }

    StringListDelegate { aspect: root.aspects.skipUpdateCheckForFilesPatterns }
    StringListDelegate { aspect: root.aspects.skipFilesPatterns }
}

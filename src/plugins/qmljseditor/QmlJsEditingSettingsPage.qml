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
        title: qsTr("Formatting")

        ColumnLayout {
            BoolDelegate { aspect: aspects.AutoFormatOnSave }
            BoolDelegate { aspect: aspects.AutoFormatOnlyCurrentProject }
        }
    }

    AspectGroupBox {
        title: qsTr("Qt Quick Toolbars")

        ColumnLayout {
            BoolDelegate { aspect: aspects.ContextPanePinned }
            BoolDelegate { aspect: aspects.ContextPaneEnabled }
        }
    }

    AspectGroupBox {
        title: qsTr("Qt Design Studio")
        // The whole group goes when there is no Design Studio to point at.
        visible: aspects.qdsCommand?.visible ?? true

        ColumnLayout {
            TextDisplayDelegate { aspect: aspects.QdsHint }
            StringDelegate { aspect: aspects.qdsCommand }
            ButtonDelegate { aspect: aspects.QdsInstall }
        }
    }

    AspectGroupBox {
        title: qsTr("Features")

        ColumnLayout {
            BoolDelegate { aspect: aspects.FoldAuxData }
            SelectionDelegate { aspect: aspects.openUiQmlMode }
        }
    }

    AspectGroupBox {
        title: qsTr("QML Language Server")

        ButtonDelegate { aspect: aspects.OpenLanguageServerSettings }
    }

    AspectGroupBox {
        title: qsTr("Static Analyzer")

        ColumnLayout {
            BoolDelegate { aspect: aspects.useCustomAnalyzer }
            TableDelegate { aspect: aspects.AnalyzerMessages }
            ButtonDelegate { aspect: aspects.ResetAnalyzerMessages }
        }
    }
}

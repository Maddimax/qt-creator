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

    contentFillsHeight: true
    AspectGroupBox {
        title: qsTr("Formatting")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.AutoFormatOnSave }
            BoolDelegate { aspect: root.aspects.AutoFormatOnlyCurrentProject }
        }
    }

    AspectGroupBox {
        title: qsTr("Qt Quick Toolbars")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.ContextPanePinned }
            BoolDelegate { aspect: root.aspects.ContextPaneEnabled }
        }
    }

    AspectGroupBox {
        title: qsTr("Qt Design Studio")
        // The whole group goes when there is no Design Studio to point at.
        visible: root.aspects.qdsCommand?.visible ?? true

        ColumnLayout {
            TextDisplayDelegate { aspect: root.aspects.QdsHint }
            StringDelegate { aspect: root.aspects.qdsCommand }
            ButtonDelegate { aspect: root.aspects.QdsInstall }
        }
    }

    AspectGroupBox {
        title: qsTr("Features")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.FoldAuxData }
            SelectionDelegate { aspect: root.aspects.openUiQmlMode }
        }
    }

    AspectGroupBox {
        title: qsTr("QML Language Server")

        ButtonDelegate { aspect: root.aspects.OpenLanguageServerSettings }
    }

    AspectGroupBox {
        title: qsTr("Static Analyzer")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.useCustomAnalyzer }
            TableDelegate { aspect: root.aspects.AnalyzerMessages }
            ButtonDelegate { aspect: root.aspects.ResetAnalyzerMessages }
        }
    }
}

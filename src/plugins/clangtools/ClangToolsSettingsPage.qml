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
        title: qsTr("Executables")

        ColumnLayout {
            StringDelegate { aspect: aspects.ClangTidyExecutable }
            StringDelegate { aspect: aspects.ClazyStandaloneExecutable }
        }
    }

    AspectGroupBox {
        title: qsTr("Run Options")

        ColumnLayout {
            TextWithActionDelegate { aspect: aspects.DiagnosticConfig }
            BoolDelegate { aspect: aspects.PreferConfigFile }
            BoolDelegate { aspect: aspects.BuildBeforeAnalysis }
            BoolDelegate { aspect: aspects.AnalyzeOpenFiles }
            IntegerDelegate { aspect: aspects.ParallelJobs }
        }
    }
}

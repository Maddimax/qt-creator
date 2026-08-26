// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// How a run is set up. The global page and a project's panel show the same
// things and differ only in whose container they are shown for.
AspectGroupBox {
    id: root

    // A NamedAspects for whichever RunSettings is being shown.
    required property var aspects

    title: qsTr("Run Options")

    ColumnLayout {
        TextWithActionDelegate { aspect: root.aspects.DiagnosticConfig }
        BoolDelegate { aspect: root.aspects.PreferConfigFile }
        BoolDelegate { aspect: root.aspects.BuildBeforeAnalysis }
        BoolDelegate { aspect: root.aspects.AnalyzeOpenFiles }
        IntegerDelegate { aspect: root.aspects.ParallelJobs }
    }
}

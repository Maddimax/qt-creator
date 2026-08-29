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

    BoolDelegate { aspect: root.aspects.NoCrashhandlerOnDebug }
    BoolDelegate { aspect: root.aspects.UseXMLOutput }
    BoolDelegate { aspect: root.aspects.VerboseBench }
    BoolDelegate { aspect: root.aspects.LogSignalsSlots }

    RowLayout {
        BoolDelegate { aspect: root.aspects.LimitWarnings }
        IntegerDelegate { aspect: root.aspects.MaxWarnings; compact: true }
    }

    AspectGroupBox {
        title: qsTr("Benchmark Metrics")

        RadioGroupDelegate { aspect: root.aspects.Metrics }
    }

    BoolDelegate { aspect: root.aspects.QuickCheckForDerivedTests }
    BoolDelegate { aspect: root.aspects.ParseMessages }
}

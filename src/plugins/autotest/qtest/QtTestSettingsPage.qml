// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    BoolDelegate { aspect: aspects.NoCrashhandlerOnDebug }
    BoolDelegate { aspect: aspects.UseXMLOutput }
    BoolDelegate { aspect: aspects.VerboseBench }
    BoolDelegate { aspect: aspects.LogSignalsSlots }

    RowLayout {
        BoolDelegate { aspect: aspects.LimitWarnings }
        IntegerDelegate { aspect: aspects.MaxWarnings }
    }

    GroupBox {
        title: qsTr("Benchmark Metrics")
        Layout.fillWidth: true

        ColumnLayout {
            SelectionDelegate { aspect: aspects.Metrics }
        }
    }

    BoolDelegate { aspect: aspects.QuickCheckForDerivedTests }
    BoolDelegate { aspect: aspects.ParseMessages }
}

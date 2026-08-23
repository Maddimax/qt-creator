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
        title: qsTr("General")

        ColumnLayout {
            IntegerDelegate { aspect: aspects.WatchdogTimeout }
            BoolDelegate { aspect: aspects.SkipKnownFrames }
            BoolDelegate { aspect: aspects.UseMessageBoxForSignals }
            BoolDelegate { aspect: aspects.AdjustBreakpointLocations }
            BoolDelegate { aspect: aspects.UseDynamicType }
            BoolDelegate { aspect: aspects.LoadGdbInit }
            BoolDelegate { aspect: aspects.LoadGdbDumpers2 }
            BoolDelegate { aspect: aspects.IntelFlavor }
            BoolDelegate { aspect: aspects.UsePseudoTracepoints }
            BoolDelegate { aspect: aspects.UseIndexCache }
            SelectionDelegate { aspect: aspects.UseDebugInfoD }
        }
    }

    AspectGroupBox {
        title: qsTr("Extended")

        ColumnLayout {
            TextDisplayDelegate { aspect: aspects.ExtendedWarning }
            BoolDelegate { aspect: aspects.TargetAsync }
            BoolDelegate { aspect: aspects.AutoEnrichParameters }
            BoolDelegate { aspect: aspects.BreakOnWarning }
            BoolDelegate { aspect: aspects.BreakOnFatal }
            BoolDelegate { aspect: aspects.BreakOnAbort }
            BoolDelegate { aspect: aspects.EnableReverseDebugging }
            BoolDelegate { aspect: aspects.MultiInferior }
        }
    }

    AspectGroupBox {
        title: qsTr("Additional Startup Commands")

        TextAreaDelegate { aspect: aspects.GdbStartupCommands }
    }

    AspectGroupBox {
        title: qsTr("Additional Attach Commands")

        TextAreaDelegate { aspect: aspects.GdbPostAttachCommands }
    }
}

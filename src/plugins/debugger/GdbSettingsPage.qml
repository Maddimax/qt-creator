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

    AspectGroupBox {
        title: qsTr("General")

        ColumnLayout {
            IntegerDelegate { aspect: root.aspects.WatchdogTimeout }
            BoolDelegate { aspect: root.aspects.SkipKnownFrames }
            BoolDelegate { aspect: root.aspects.UseMessageBoxForSignals }
            BoolDelegate { aspect: root.aspects.AdjustBreakpointLocations }
            BoolDelegate { aspect: root.aspects.UseDynamicType }
            BoolDelegate { aspect: root.aspects.LoadGdbInit }
            BoolDelegate { aspect: root.aspects.LoadGdbDumpers2 }
            BoolDelegate { aspect: root.aspects.IntelFlavor }
            BoolDelegate { aspect: root.aspects.UsePseudoTracepoints }
            BoolDelegate { aspect: root.aspects.UseIndexCache }
            SelectionDelegate { aspect: root.aspects.UseDebugInfoD }
        }
    }

    AspectGroupBox {
        title: qsTr("Extended")

        ColumnLayout {
            TextDisplayDelegate { aspect: root.aspects.ExtendedWarning }
            BoolDelegate { aspect: root.aspects.TargetAsync }
            BoolDelegate { aspect: root.aspects.AutoEnrichParameters }
            BoolDelegate { aspect: root.aspects.BreakOnWarning }
            BoolDelegate { aspect: root.aspects.BreakOnFatal }
            BoolDelegate { aspect: root.aspects.BreakOnAbort }
            BoolDelegate { aspect: root.aspects.EnableReverseDebugging }
            BoolDelegate { aspect: root.aspects.MultiInferior }
        }
    }

    AspectGroupBox {
        title: qsTr("Additional Startup Commands")

        TextAreaDelegate { aspect: root.aspects.GdbStartupCommands }
    }

    AspectGroupBox {
        title: qsTr("Additional Attach Commands")

        TextAreaDelegate { aspect: root.aspects.GdbPostAttachCommands }
    }
}

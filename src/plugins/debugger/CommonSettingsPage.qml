// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {

    contentFillsHeight: true
    AspectGroupBox {
        title: qsTr("Behavior")

        ColumnLayout {
            // Only Windows has a post-mortem debugger to register with; the
            // aspect is invisible elsewhere and the delegate follows that.
            BoolDelegate { aspect: aspects.RegisterForPostMortem }
            BoolDelegate { aspect: aspects.RaiseOnInterrupt }
            BoolDelegate { aspect: aspects.WarnOnReleaseBuilds }
            BoolDelegate { aspect: aspects.BreakpointsFullPath }
            BoolDelegate { aspect: aspects.ResolveBreakpointSymlinks }
            BoolDelegate { aspect: aspects.ForceLoggingToConsole }
            BoolDelegate { aspect: aspects.UseNativeCombinedDebugging }
            BoolDelegate { aspect: aspects.CollapseDebuggerMachineryFrames }
            IntegerDelegate { aspect: aspects.MaximalStackDepth }
        }
    }

    AspectGroupBox {
        title: qsTr("When Debugging Stops")

        ColumnLayout {
            BoolDelegate { aspect: aspects.CloseBuffersOnExit }
            BoolDelegate { aspect: aspects.CloseMemoryBuffersOnExit }
            BoolDelegate { aspect: aspects.SwitchModeOnExit }
        }
    }

    AspectGroupBox {
        title: qsTr("User Interface")

        ColumnLayout {
            BoolDelegate { aspect: aspects.UseAnnotations }
            BoolDelegate { aspect: aspects.UseToolTips }
            BoolDelegate { aspect: aspects.UseAlternatingRowColours }
            BoolDelegate { aspect: aspects.FontSizeFollowsEditor }
            BoolDelegate { aspect: aspects.StationaryEditorWhileStepping }
            BoolDelegate { aspect: aspects.ShowQmlObjectTree }
            BoolDelegate { aspect: aspects.ShowUnsupportedBreakpointWarning }
        }
    }

    TableDelegate { aspect: aspects.SourcePathMap }
    ButtonDelegate { aspect: aspects.AddQtSources }
}

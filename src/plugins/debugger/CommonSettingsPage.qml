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
        title: qsTr("Behavior")

        ColumnLayout {
            // Only Windows has a post-mortem debugger to register with; the
            // aspect is invisible elsewhere and the delegate follows that.
            BoolDelegate { aspect: root.aspects.RegisterForPostMortem }
            BoolDelegate { aspect: root.aspects.RaiseOnInterrupt }
            BoolDelegate { aspect: root.aspects.WarnOnReleaseBuilds }
            BoolDelegate { aspect: root.aspects.BreakpointsFullPath }
            BoolDelegate { aspect: root.aspects.ResolveBreakpointSymlinks }
            BoolDelegate { aspect: root.aspects.ForceLoggingToConsole }
            BoolDelegate { aspect: root.aspects.UseNativeCombinedDebugging }
            BoolDelegate { aspect: root.aspects.CollapseDebuggerMachineryFrames }
            IntegerDelegate { aspect: root.aspects.MaximalStackDepth }
        }
    }

    AspectGroupBox {
        title: qsTr("When Debugging Stops")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.CloseBuffersOnExit }
            BoolDelegate { aspect: root.aspects.CloseMemoryBuffersOnExit }
            BoolDelegate { aspect: root.aspects.SwitchModeOnExit }
        }
    }

    AspectGroupBox {
        title: qsTr("User Interface")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.UseAnnotations }
            BoolDelegate { aspect: root.aspects.UseToolTips }
            BoolDelegate { aspect: root.aspects.UseAlternatingRowColours }
            BoolDelegate { aspect: root.aspects.FontSizeFollowsEditor }
            BoolDelegate { aspect: root.aspects.StationaryEditorWhileStepping }
            BoolDelegate { aspect: root.aspects.ShowQmlObjectTree }
            BoolDelegate { aspect: root.aspects.ShowUnsupportedBreakpointWarning }
        }
    }

    TableDelegate { aspect: root.aspects.SourcePathMap }
    ButtonDelegate { aspect: root.aspects.AddQtSources }
}

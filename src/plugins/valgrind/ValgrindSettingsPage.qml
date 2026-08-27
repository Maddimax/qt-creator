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
        title: qsTr("Valgrind Generic Settings")

        ColumnLayout {
            StringDelegate { aspect: root.aspects.ValgrindExecutable }
            StringDelegate { aspect: root.aspects.ValgrindArguments }
            SelectionDelegate { aspect: root.aspects.SelfModifyingCodeDetection }
        }
    }

    AspectGroupBox {
        title: qsTr("Memcheck Memory Analysis Options")

        ColumnLayout {
            StringDelegate { aspect: root.aspects.MemcheckArguments }
            BoolDelegate { aspect: root.aspects.TrackOrigins }
            BoolDelegate { aspect: root.aspects.ShowReachable }
            SelectionDelegate { aspect: root.aspects.LeakCheckOnFinish }
            IntegerDelegate { aspect: root.aspects.NumCallers }
            BoolDelegate { aspect: root.aspects.FilterExternalIssues }
            FilePathListDelegate { aspect: root.aspects.SuppressionFiles }
        }
    }

    AspectGroupBox {
        title: qsTr("Callgrind Profiling Options")

        ColumnLayout {
            StringDelegate { aspect: root.aspects.CallgrindArguments }
            StringDelegate { aspect: root.aspects.KCachegrindExecutable }
            DoubleDelegate { aspect: root.aspects.MinimumCostRatio }
            DoubleDelegate { aspect: root.aspects.VisualisationMinimumCostRatio }
            BoolDelegate { aspect: root.aspects.EnableEventToolTips }

            AspectGroupBox {
                ColumnLayout {
                    BoolDelegate { aspect: root.aspects.EnableCacheSim }
                    BoolDelegate { aspect: root.aspects.EnableBranchSim }
                    BoolDelegate { aspect: root.aspects.CollectSystime }
                    BoolDelegate { aspect: root.aspects.CollectBusEvents }
                }
            }
        }
    }
}

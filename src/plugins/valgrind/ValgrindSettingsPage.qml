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
        title: qsTr("Valgrind Generic Settings")

        ColumnLayout {
            StringDelegate { aspect: aspects.ValgrindExecutable }
            StringDelegate { aspect: aspects.ValgrindArguments }
            SelectionDelegate { aspect: aspects.SelfModifyingCodeDetection }
        }
    }

    AspectGroupBox {
        title: qsTr("Memcheck Memory Analysis Options")

        ColumnLayout {
            StringDelegate { aspect: aspects.MemcheckArguments }
            BoolDelegate { aspect: aspects.TrackOrigins }
            BoolDelegate { aspect: aspects.ShowReachable }
            SelectionDelegate { aspect: aspects.LeakCheckOnFinish }
            IntegerDelegate { aspect: aspects.NumCallers }
            BoolDelegate { aspect: aspects.FilterExternalIssues }
            FilePathListDelegate { aspect: aspects.SuppressionFiles }
        }
    }

    AspectGroupBox {
        title: qsTr("Callgrind Profiling Options")

        ColumnLayout {
            StringDelegate { aspect: aspects.CallgrindArguments }
            StringDelegate { aspect: aspects.KCachegrindExecutable }
            DoubleDelegate { aspect: aspects.MinimumCostRatio }
            DoubleDelegate { aspect: aspects.VisualisationMinimumCostRatio }
            BoolDelegate { aspect: aspects.EnableEventToolTips }

            AspectGroupBox {
                ColumnLayout {
                    BoolDelegate { aspect: aspects.EnableCacheSim }
                    BoolDelegate { aspect: aspects.EnableBranchSim }
                    BoolDelegate { aspect: aspects.CollectSystime }
                    BoolDelegate { aspect: aspects.CollectBusEvents }
                }
            }
        }
    }
}

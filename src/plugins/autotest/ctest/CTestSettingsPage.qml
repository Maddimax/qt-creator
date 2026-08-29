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

    BoolDelegate { aspect: root.aspects.OutputOnFail }
    BoolDelegate { aspect: root.aspects.ScheduleRandom }
    BoolDelegate { aspect: root.aspects.StopOnFail }
    SelectionDelegate { aspect: root.aspects.OutputMode }

    AspectGroupBox {
        title: qsTr("Repeat Tests")
        checkAspect: root.aspects.Repeat

        RowLayout {
            SelectionDelegate { aspect: root.aspects.RepetitionMode }
            IntegerDelegate { aspect: root.aspects.RepetitionCount }
        }
    }

    AspectGroupBox {
        title: qsTr("Run in Parallel")
        checkAspect: root.aspects.Parallel

        ColumnLayout {
            IntegerDelegate { aspect: root.aspects.Jobs }

            RowLayout {
                BoolDelegate { aspect: root.aspects.TestLoad }
                IntegerDelegate { aspect: root.aspects.Threshold; compact: true }
            }
        }
    }
}

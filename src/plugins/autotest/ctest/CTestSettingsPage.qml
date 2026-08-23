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

    BoolDelegate { aspect: aspects.OutputOnFail }
    BoolDelegate { aspect: aspects.ScheduleRandom }
    BoolDelegate { aspect: aspects.StopOnFail }
    SelectionDelegate { aspect: aspects.OutputMode }

    AspectGroupBox {
        title: qsTr("Repeat Tests")
        checkAspect: aspects.Repeat

        RowLayout {
            SelectionDelegate { aspect: aspects.RepetitionMode }
            IntegerDelegate { aspect: aspects.RepetitionCount }
        }
    }

    AspectGroupBox {
        title: qsTr("Run in Parallel")
        checkAspect: aspects.Parallel

        ColumnLayout {
            IntegerDelegate { aspect: aspects.Jobs }

            RowLayout {
                BoolDelegate { aspect: aspects.TestLoad }
                IntegerDelegate { aspect: aspects.Threshold }
            }
        }
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// One target, both captures: the native sampler and the QML profiler run
// against the same launched executable, so the launch fields are shared and
// each capture contributes its own group.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Executable }
    StringDelegate { aspect: root.aspects.Arguments }
    StringDelegate { aspect: root.aspects.WorkingDirectory }

    AspectGroupBox {
        title: qsTr("CPU Sampler")

        IntegerDelegate { aspect: root.aspects.IntervalUs }
    }

    AspectGroupBox {
        title: qsTr("QML Profiler")

        // One toggle per profiler feature. Repeated over the container rather
        // than named one by one: how many there are is QmlDebug's business.
        Flow {
            width: parent.width
            spacing: Spacing.GapHM

            Repeater {
                model: AspectModels.container(root.aspects.Features)
                delegate: BoolDelegate {}
            }
        }
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// What the QML profiler backend records, and what it records it from: either a
// running QML debug server or the executable below. Which of the two is live is
// the settings object's business - it enables the launch fields from
// connectToServer - so this only lists them.
AspectPage {
    id: root

    BoolDelegate { aspect: root.aspects.ConnectToServer }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        StringDelegate { aspect: root.aspects.Host }
        IntegerDelegate { aspect: root.aspects.Port }
    }

    StringDelegate { aspect: root.aspects.Executable }
    StringDelegate { aspect: root.aspects.Arguments }
    StringDelegate { aspect: root.aspects.WorkingDirectory }

    AspectGroupBox {
        title: qsTr("Record")

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

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
        title: qsTr("Configuration")

        ColumnLayout {
            StringDelegate { aspect: root.aspects.BinaryPath }
            StringDelegate { aspect: root.aspects.Root }
        }
    }

    AspectGroupBox {
        title: qsTr("Miscellaneous")

        ColumnLayout {
            IntegerDelegate { aspect: root.aspects.Timeout }
            StringDelegate { aspect: root.aspects.DiffOptions }
            BoolDelegate { aspect: root.aspects.DescribeByCommitId }
        }
    }
}

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

        StringDelegate { aspect: root.aspects.Command }
    }

    AspectGroupBox {
        title: qsTr("Environment Variables")
        checkAspect: root.aspects.Default

        ColumnLayout {
            StringDelegate { aspect: root.aspects.Port }
            StringDelegate { aspect: root.aspects.Client }
            StringDelegate { aspect: root.aspects.User }
        }
    }

    AspectGroupBox {
        title: qsTr("Miscellaneous")

        ColumnLayout {
            RowLayout {
                IntegerDelegate { aspect: root.aspects.LogCount }
                IntegerDelegate { aspect: root.aspects.TimeOut }
            }

            BoolDelegate { aspect: root.aspects.PromptToOpen }
        }
    }

    RowLayout {
        TextDisplayDelegate { aspect: root.aspects.TestResult }
        ButtonDelegate { aspect: root.aspects.Test }
    }
}

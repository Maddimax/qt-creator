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
        title: qsTr("Configuration")

        StringDelegate { aspect: aspects.Command }
    }

    AspectGroupBox {
        title: qsTr("Environment Variables")
        checkAspect: aspects.Default

        ColumnLayout {
            StringDelegate { aspect: aspects.Port }
            StringDelegate { aspect: aspects.Client }
            StringDelegate { aspect: aspects.User }
        }
    }

    AspectGroupBox {
        title: qsTr("Miscellaneous")

        ColumnLayout {
            RowLayout {
                IntegerDelegate { aspect: aspects.LogCount }
                IntegerDelegate { aspect: aspects.TimeOut }
            }

            BoolDelegate { aspect: aspects.PromptToOpen }
        }
    }

    RowLayout {
        TextDisplayDelegate { aspect: aspects.TestResult }
        ButtonDelegate { aspect: aspects.Test }
    }
}

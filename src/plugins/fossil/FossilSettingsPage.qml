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

        StringDelegate { aspect: aspects.BinaryPath }
    }

    AspectGroupBox {
        title: qsTr("Local Repositories")

        StringDelegate { aspect: aspects.defaultRepoPath }
    }

    AspectGroupBox {
        title: qsTr("User")

        ColumnLayout {
            StringDelegate { aspect: aspects.Username }
            StringDelegate { aspect: aspects.sslIdentityFile }
        }
    }

    AspectGroupBox {
        title: qsTr("Miscellaneous")

        ColumnLayout {
            RowLayout {
                IntegerDelegate { aspect: aspects.LogCount }
                IntegerDelegate { aspect: aspects.timelineWidth }
                IntegerDelegate { aspect: aspects.Timeout }
            }
            BoolDelegate { aspect: aspects.disableAutosync }
        }
    }
}

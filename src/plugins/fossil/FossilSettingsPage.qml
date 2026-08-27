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

        StringDelegate { aspect: root.aspects.BinaryPath }
    }

    AspectGroupBox {
        title: qsTr("Local Repositories")

        StringDelegate { aspect: root.aspects.defaultRepoPath }
    }

    AspectGroupBox {
        title: qsTr("User")

        ColumnLayout {
            StringDelegate { aspect: root.aspects.Username }
            StringDelegate { aspect: root.aspects.sslIdentityFile }
        }
    }

    AspectGroupBox {
        title: qsTr("Miscellaneous")

        ColumnLayout {
            RowLayout {
                IntegerDelegate { aspect: root.aspects.LogCount }
                IntegerDelegate { aspect: root.aspects.timelineWidth }
                IntegerDelegate { aspect: root.aspects.Timeout }
            }
            BoolDelegate { aspect: root.aspects.disableAutosync }
        }
    }
}

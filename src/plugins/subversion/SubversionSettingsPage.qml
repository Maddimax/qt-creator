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
        title: qsTr("Authentication")
        checkAspect: aspects.Authentication

        ColumnLayout {
            StringDelegate { aspect: aspects.User }
            StringDelegate { aspect: aspects.Password }
        }
    }

    AspectGroupBox {
        title: qsTr("Miscellaneous")

        ColumnLayout {
            RowLayout {
                IntegerDelegate { aspect: aspects.LogCount }
                IntegerDelegate { aspect: aspects.Timeout }
            }
            BoolDelegate { aspect: aspects.SpaceIgnorantAnnotation }
        }
    }
}

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
        title: qsTr("Emscripten SDK path:")

        ColumnLayout {
            TextDisplayDelegate { aspect: root.aspects.Instruction }
            StringDelegate { aspect: root.aspects.EmSdk }
            TextDisplayDelegate { aspect: root.aspects.StatusIsEmsdkDir }
            TextDisplayDelegate { aspect: root.aspects.StatusSdkInstalled }
            TextDisplayDelegate { aspect: root.aspects.StatusSdkActivated }
            TextDisplayDelegate { aspect: root.aspects.StatusSdkInvalid }
            TextDisplayDelegate { aspect: root.aspects.EmSdkVersionDisplay }
        }
    }

    AspectGroupBox {
        title: qsTr("Emscripten SDK environment:")

        TextAreaDelegate { aspect: root.aspects.EmSdkEnvDisplay }
    }

    TextDisplayDelegate { aspect: root.aspects.QtVersionDisplay }
}

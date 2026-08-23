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
        title: qsTr("Emscripten SDK path:")

        ColumnLayout {
            TextDisplayDelegate { aspect: aspects.Instruction }
            StringDelegate { aspect: aspects.EmSdk }
            TextDisplayDelegate { aspect: aspects.StatusIsEmsdkDir }
            TextDisplayDelegate { aspect: aspects.StatusSdkInstalled }
            TextDisplayDelegate { aspect: aspects.StatusSdkActivated }
            TextDisplayDelegate { aspect: aspects.StatusSdkInvalid }
            TextDisplayDelegate { aspect: aspects.EmSdkVersionDisplay }
        }
    }

    AspectGroupBox {
        title: qsTr("Emscripten SDK environment:")

        TextAreaDelegate { aspect: aspects.EmSdkEnvDisplay }
    }

    TextDisplayDelegate { aspect: aspects.QtVersionDisplay }
}

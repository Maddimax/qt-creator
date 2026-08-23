// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    BoolDelegate { aspect: aspects.UseDebuggingHelper }
    BoolDelegate { aspect: aspects.AllowInferiorCalls }

    TextDisplayDelegate { aspect: aspects.HelpersNote }
    BoolDelegate { aspect: aspects.UseCodeModel }
    BoolDelegate { aspect: aspects.ShowThreadNames }

    AspectGroupBox {
        title: qsTr("Extra Debugging Helper")

        StringDelegate { aspect: aspects.ExtraDumperFile }
    }

    AspectGroupBox {
        title: qsTr("Debugging Helper Customization")

        StringDelegate { aspect: aspects.GdbCustomDumperCommands }
    }

    BoolDelegate { aspect: aspects.ShowStandardNamespace }
    BoolDelegate { aspect: aspects.ShowQtNamespace }
    BoolDelegate { aspect: aspects.ShowQObjectNames2 }

    IntegerDelegate { aspect: aspects.MaximalStringLength }
    IntegerDelegate { aspect: aspects.DisplayStringLimit }
    IntegerDelegate { aspect: aspects.DefaultArraySize }
}

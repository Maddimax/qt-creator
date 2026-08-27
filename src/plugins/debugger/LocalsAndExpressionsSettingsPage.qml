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

    BoolDelegate { aspect: root.aspects.UseDebuggingHelper }
    BoolDelegate { aspect: root.aspects.AllowInferiorCalls }

    TextDisplayDelegate { aspect: root.aspects.HelpersNote }
    BoolDelegate { aspect: root.aspects.UseCodeModel }
    BoolDelegate { aspect: root.aspects.ShowThreadNames }

    AspectGroupBox {
        title: qsTr("Extra Debugging Helper")

        StringDelegate { aspect: root.aspects.ExtraDumperFile }
    }

    AspectGroupBox {
        title: qsTr("Debugging Helper Customization")

        TextAreaDelegate { aspect: root.aspects.GdbCustomDumperCommands }
    }

    BoolDelegate { aspect: root.aspects.ShowStandardNamespace }
    BoolDelegate { aspect: root.aspects.ShowQtNamespace }
    BoolDelegate { aspect: root.aspects.ShowQObjectNames2 }

    IntegerDelegate { aspect: root.aspects.MaximalStringLength }
    IntegerDelegate { aspect: root.aspects.DisplayStringLimit }
    IntegerDelegate { aspect: root.aspects.DefaultArraySize }
}

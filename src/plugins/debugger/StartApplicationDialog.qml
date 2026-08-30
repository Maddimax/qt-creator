// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// What to debug and how to reach it. Which of these are shown depends on
// whether a server is already running, which the dialog decides.
AspectPage {
    id: root

    InlineGroupDelegate { aspect: root.aspects.Kit }
    IntegerDelegate { aspect: root.aspects.ServerPort }
    StringDelegate { aspect: root.aspects.LocalExecutable }
    StringDelegate { aspect: root.aspects.Arguments }
    StringDelegate { aspect: root.aspects.WorkingDirectory }
    BoolDelegate { aspect: root.aspects.RunInTerminal }
    BoolDelegate { aspect: root.aspects.BreakAtMain }
    BoolDelegate { aspect: root.aspects.UseTargetExtendedRemote }
    StringDelegate { aspect: root.aspects.SysRoot }
    TextAreaDelegate { aspect: root.aspects.InitCommands }
    TextAreaDelegate { aspect: root.aspects.ResetCommands }
    StringDelegate { aspect: root.aspects.DebugInfo }
    TextDisplayDelegate { aspect: root.aspects.ChannelHint }
    StringDelegate { aspect: root.aspects.ChannelOverride }

    // The rule the form layout drew before the list of recent runs: what is
    // below it is a way to fill the fields above, not another field.
    Rectangle {
        color: Tokens.strokeSubtle
        implicitHeight: 1
        Layout.fillWidth: true
    }

    SelectionDelegate { aspect: root.aspects.Recent }
}

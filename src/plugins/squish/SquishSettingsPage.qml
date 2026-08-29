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

    StringDelegate { aspect: root.aspects.SquishPath }
    StringDelegate { aspect: root.aspects.LicensePath }

    RowLayout {
        BoolDelegate { aspect: root.aspects.Local }
        StringDelegate { aspect: root.aspects.ServerHost; compact: true }
        IntegerDelegate { aspect: root.aspects.ServerPort; compact: true }
    }

    BoolDelegate { aspect: root.aspects.Verbose }
    BoolDelegate { aspect: root.aspects.MinimizeIDE }
}

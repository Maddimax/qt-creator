// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    TriStateDelegate { aspect: root.aspects.CppDebugger }

    // The QML row says what it needs of the project, with a link into the
    // manual.
    RowLayout {
        TriStateDelegate { aspect: root.aspects.QmlDebugger }
        TextDisplayDelegate { aspect: root.aspects.QmlPrerequisites }
    }

    TriStateDelegate { aspect: root.aspects.PythonDebugger }

    StringDelegate { aspect: root.aspects.OverrideStartup }

    // Only where QTC_DEBUGGER_MULTIPROCESS asks for it, which the aspect
    // decides.
    BoolDelegate { aspect: root.aspects.MultiProcess }
}

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

    StringDelegate { aspect: root.aspects.binary }

    Label { text: qsTr("Checks:") }

    Flow {
        Layout.fillWidth: true
        Layout.leftMargin: Spacing.PaddingHL
        spacing: Spacing.GapHM

        BoolDelegate { aspect: root.aspects.warning }
        BoolDelegate { aspect: root.aspects.style }
        BoolDelegate { aspect: root.aspects.performance }
        BoolDelegate { aspect: root.aspects.portability }
        BoolDelegate { aspect: root.aspects.information }
        BoolDelegate { aspect: root.aspects.unusedFunction }
        BoolDelegate { aspect: root.aspects.missingInclude }
    }

    StringDelegate { aspect: root.aspects.customArguments }
    StringDelegate { aspect: root.aspects.ignoredPatterns }

    Flow {
        Layout.fillWidth: true
        spacing: Spacing.GapHM

        BoolDelegate { aspect: root.aspects.inconclusive }
        BoolDelegate { aspect: root.aspects.forceDefines }
        BoolDelegate { aspect: root.aspects.showOutput }
        BoolDelegate { aspect: root.aspects.addIncludePaths }
        BoolDelegate { aspect: root.aspects.guessArguments }
    }
}

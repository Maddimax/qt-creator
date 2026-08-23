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

    StringDelegate { aspect: aspects.binary }

    Label { text: qsTr("Checks:") }

    Flow {
        Layout.fillWidth: true
        Layout.leftMargin: Spacing.PaddingHL
        spacing: Spacing.GapHM

        BoolDelegate { aspect: aspects.warning }
        BoolDelegate { aspect: aspects.style }
        BoolDelegate { aspect: aspects.performance }
        BoolDelegate { aspect: aspects.portability }
        BoolDelegate { aspect: aspects.information }
        BoolDelegate { aspect: aspects.unusedFunction }
        BoolDelegate { aspect: aspects.missingInclude }
    }

    StringDelegate { aspect: aspects.customArguments }
    StringDelegate { aspect: aspects.ignoredPatterns }

    Flow {
        Layout.fillWidth: true
        spacing: Spacing.GapHM

        BoolDelegate { aspect: aspects.inconclusive }
        BoolDelegate { aspect: aspects.forceDefines }
        BoolDelegate { aspect: aspects.showOutput }
        BoolDelegate { aspect: aspects.addIncludePaths }
        BoolDelegate { aspect: aspects.guessArguments }
    }
}

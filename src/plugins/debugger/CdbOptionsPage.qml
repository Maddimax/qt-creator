// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// How CDB is started, what it stops on, and what reaches the Issues view.
AspectPage {
    id: root

    RowLayout {
        spacing: Spacing.GapHM

        AspectGroupBox {
            title: qsTr("Startup")
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop

            StringDelegate { aspect: root.aspects.AdditionalArguments }
            BoolDelegate { aspect: root.aspects.UseCdbConsole }
        }

        AspectGroupBox {
            title: qsTr("Various")
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop

            BoolDelegate { aspect: root.aspects.IgnoreFirstChanceAccessViolation }
            BoolDelegate { aspect: root.aspects.BreakOnCrtDbgReport }
            BoolDelegate { aspect: root.aspects.BreakPointCorrection }
            BoolDelegate { aspect: root.aspects.UsePythonDumper }
            BoolDelegate { aspect: root.aspects.EnableHeapDebugging }
        }
    }

    // The rows are the events CDB knows about, so this is a table of check
    // states and filter text rather than a list to add to.
    AspectGroupBox {
        title: qsTr("Break On")

        TableDelegate { aspect: root.aspects.BreakEvents }
    }

    AspectGroupBox {
        title: qsTr("Add Exceptions to Issues View")

        BoolDelegate { aspect: root.aspects.FirstChanceExceptionTaskEntry }
        BoolDelegate { aspect: root.aspects.SecondChanceExceptionTaskEntry }
    }
}

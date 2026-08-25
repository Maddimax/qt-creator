// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

AspectPage {
    id: root

    readonly property var updates: AspectModels.named(aspects.Updates)

    // Whether to check at all is the group's own check box.
    AspectGroupBox {
        title: root.aspects.Updates?.plainLabelText ?? ""
        checkAspect: root.aspects.AutomaticCheck

        TextDisplayDelegate { aspect: root.updates.Info }
        SelectionDelegate { aspect: root.updates.Interval }
        TextDisplayDelegate { aspect: root.updates.NextCheckDate }
        BoolDelegate { aspect: root.updates.CheckForNewQtVersions }
    }

    // What the last check found, and the button that runs another.
    TextDisplayDelegate { aspect: root.aspects.LastCheckDate }
    TextWithActionDelegate { aspect: root.aspects.CheckNow }
}

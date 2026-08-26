// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// What the native call-stack sampler records, and what it records from: either
// a process picked while running or the executable below. Which of the two is
// live is the settings object's business, so this only lists them.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Executable }
    StringDelegate { aspect: root.aspects.Arguments }
    StringDelegate { aspect: root.aspects.WorkingDirectory }
    IntegerDelegate { aspect: root.aspects.IntervalUs }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        BoolDelegate { aspect: root.aspects.Attach }
        TextWithActionDelegate { aspect: root.aspects.PickProcess }
    }
}

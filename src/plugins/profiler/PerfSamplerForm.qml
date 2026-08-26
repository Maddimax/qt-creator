// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// What perf records from: a process picked while running, or the executable
// below. What it records is the IDE's own perf configuration, which this
// backend reuses rather than owns - see SamplerSettings::reusedSettings(), and
// the form it is drawn beside this one with.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Executable }
    StringDelegate { aspect: root.aspects.Arguments }
    StringDelegate { aspect: root.aspects.WorkingDirectory }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        BoolDelegate { aspect: root.aspects.Attach }
        TextWithActionDelegate { aspect: root.aspects.PickProcess }
    }

    BoolDelegate { aspect: root.aspects.DownloadDebugInfo }
}

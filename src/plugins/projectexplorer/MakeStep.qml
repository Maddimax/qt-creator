// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.MakeCommand }
    StringDelegate { aspect: root.aspects.MakeArguments }

    // Read as one answer: how many jobs, whether that overrides MAKEFLAGS,
    // and what the step makes of the two together.
    RowLayout {
        IntegerDelegate { aspect: root.aspects.JobCount }
        BoolDelegate { aspect: root.aspects.OverrideMakeflags }
        TextDisplayDelegate { aspect: root.aspects.MakeflagsNote }
    }

    // Only steps that say they support it, which they say by making the
    // setting visible.
    BoolDelegate { aspect: root.aspects.DisabledForSubdirs }

    MultiSelectionDelegate { aspect: root.aspects.BuildTargets }

    // Unix only, and the aspect itself decides that.
    InlineGroupDelegate { aspect: root.aspects.RunAs }
}

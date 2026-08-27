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

    BoolDelegate { aspect: root.aspects.RunDisabled }
    BoolDelegate { aspect: root.aspects.ThrowOnFailure }
    BoolDelegate { aspect: root.aspects.BreakOnFailure }

    RowLayout {
        BoolDelegate { aspect: root.aspects.Repeat }
        IntegerDelegate { aspect: root.aspects.Iterations }
    }

    RowLayout {
        BoolDelegate { aspect: root.aspects.Shuffle }
        IntegerDelegate { aspect: root.aspects.Seed }
    }

    SelectionDelegate { aspect: root.aspects.GroupMode }
    StringDelegate { aspect: root.aspects.GTestFilter }
}

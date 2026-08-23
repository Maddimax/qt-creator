// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    BoolDelegate { aspect: aspects.RunDisabled }
    BoolDelegate { aspect: aspects.ThrowOnFailure }
    BoolDelegate { aspect: aspects.BreakOnFailure }

    RowLayout {
        BoolDelegate { aspect: aspects.Repeat }
        IntegerDelegate { aspect: aspects.Iterations }
    }

    RowLayout {
        BoolDelegate { aspect: aspects.Shuffle }
        IntegerDelegate { aspect: aspects.Seed }
    }

    SelectionDelegate { aspect: aspects.GroupMode }
    StringDelegate { aspect: aspects.GTestFilter }
}

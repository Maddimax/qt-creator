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

    BoolDelegate { aspect: root.aspects.WrapAppOutput }
    BoolDelegate { aspect: root.aspects.CleanOldAppOutput }
    BoolDelegate { aspect: root.aspects.DiscardAppOutput }
    BoolDelegate { aspect: root.aspects.MergeStdErrAndStdOut }

    SelectionDelegate { aspect: root.aspects.ShowRunOutput }
    SelectionDelegate { aspect: root.aspects.ShowDebugOutput }

    IntegerDelegate { aspect: root.aspects.MaxAppOutputLines }

    RowLayout {
        BoolDelegate { aspect: root.aspects.OverwriteBackground }
        ColorDelegate { aspect: root.aspects.BackgroundColor }
    }
}

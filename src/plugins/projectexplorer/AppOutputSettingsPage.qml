// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    BoolDelegate { aspect: aspects.WrapAppOutput }
    BoolDelegate { aspect: aspects.CleanOldAppOutput }
    BoolDelegate { aspect: aspects.DiscardAppOutput }
    BoolDelegate { aspect: aspects.MergeStdErrAndStdOut }

    SelectionDelegate { aspect: aspects.ShowRunOutput }
    SelectionDelegate { aspect: aspects.ShowDebugOutput }

    IntegerDelegate { aspect: aspects.MaxAppOutputLines }

    RowLayout {
        BoolDelegate { aspect: aspects.OverwriteBackground }
        ColorDelegate { aspect: aspects.BackgroundColor }
    }
}

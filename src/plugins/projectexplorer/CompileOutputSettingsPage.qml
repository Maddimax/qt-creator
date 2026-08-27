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

    BoolDelegate { aspect: root.aspects.WrapBuildOutput }
    BoolDelegate { aspect: root.aspects.ShowCompilerOutput }
    BoolDelegate { aspect: root.aspects.DiscardCompilerOutput }
    IntegerDelegate { aspect: root.aspects.MaxBuildOutputLines }

    RowLayout {
        BoolDelegate { aspect: root.aspects.OverwriteBackground }
        ColorDelegate { aspect: root.aspects.BackgroundColor }
    }
}

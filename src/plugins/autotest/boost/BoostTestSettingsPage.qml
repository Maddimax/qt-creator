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

    SelectionDelegate { aspect: root.aspects.LogLevel }
    SelectionDelegate { aspect: root.aspects.ReportLevel }

    RowLayout {
        BoolDelegate { aspect: root.aspects.Randomize }
        IntegerDelegate { aspect: root.aspects.Seed }
    }

    BoolDelegate { aspect: root.aspects.SystemErrors }
    BoolDelegate { aspect: root.aspects.FPExceptions }
    BoolDelegate { aspect: root.aspects.MemoryLeaks }
}

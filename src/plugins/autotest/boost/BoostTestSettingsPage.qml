// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    SelectionDelegate { aspect: aspects.LogLevel }
    SelectionDelegate { aspect: aspects.ReportLevel }

    RowLayout {
        BoolDelegate { aspect: aspects.Randomize }
        IntegerDelegate { aspect: aspects.Seed }
    }

    BoolDelegate { aspect: aspects.SystemErrors }
    BoolDelegate { aspect: aspects.FPExceptions }
    BoolDelegate { aspect: aspects.MemoryLeaks }
}

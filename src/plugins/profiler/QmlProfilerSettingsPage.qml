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

    BoolDelegate { aspect: root.aspects.FlushEnabled }
    IntegerDelegate { aspect: root.aspects.FlushInterval }
    BoolDelegate { aspect: root.aspects.AggregateTraces }
    IntegerDelegate { aspect: root.aspects.CompileThresholdMs }
    IntegerDelegate { aspect: root.aspects.SyncLoadThresholdMs }
    IntegerDelegate { aspect: root.aspects.PeriodicMinCount }
    IntegerDelegate { aspect: root.aspects.PeriodicDeviationPercent }
    DoubleDelegate { aspect: root.aspects.PixmapMegapixels }
    IntegerDelegate { aspect: root.aspects.PerFrameBudgetUs }
}

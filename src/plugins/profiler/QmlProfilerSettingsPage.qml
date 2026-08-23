// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    BoolDelegate { aspect: aspects.FlushEnabled }
    IntegerDelegate { aspect: aspects.FlushInterval }
    BoolDelegate { aspect: aspects.AggregateTraces }
    IntegerDelegate { aspect: aspects.CompileThresholdMs }
    IntegerDelegate { aspect: aspects.SyncLoadThresholdMs }
    IntegerDelegate { aspect: aspects.PeriodicMinCount }
    IntegerDelegate { aspect: aspects.PeriodicDeviationPercent }
    DoubleDelegate { aspect: aspects.PixmapMegapixels }
    IntegerDelegate { aspect: aspects.PerFrameBudgetUs }
}

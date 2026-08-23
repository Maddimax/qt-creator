// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    BoolDelegate { aspect: aspects.ShowSuccess }
    BoolDelegate { aspect: aspects.BreakOnFailure }
    BoolDelegate { aspect: aspects.NoThrow }
    BoolDelegate { aspect: aspects.VisibleWS }

    RowLayout {
        BoolDelegate { aspect: aspects.AbortChecked }
        IntegerDelegate { aspect: aspects.AbortAfter }
    }

    RowLayout {
        BoolDelegate { aspect: aspects.SamplesChecked }
        IntegerDelegate { aspect: aspects.BenchSamples }
    }

    RowLayout {
        BoolDelegate { aspect: aspects.ResamplesChecked }
        IntegerDelegate { aspect: aspects.BenchResamples }
    }

    RowLayout {
        BoolDelegate { aspect: aspects.ConfIntChecked }
        DoubleDelegate { aspect: aspects.BenchConfInt }
    }

    RowLayout {
        BoolDelegate { aspect: aspects.WarmupChecked }
        IntegerDelegate { aspect: aspects.BenchWarmup }
    }

    BoolDelegate { aspect: aspects.NoAnalysis }
}

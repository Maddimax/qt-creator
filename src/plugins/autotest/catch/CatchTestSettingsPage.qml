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

    BoolDelegate { aspect: root.aspects.ShowSuccess }
    BoolDelegate { aspect: root.aspects.BreakOnFailure }
    BoolDelegate { aspect: root.aspects.NoThrow }
    BoolDelegate { aspect: root.aspects.VisibleWS }

    RowLayout {
        BoolDelegate { aspect: root.aspects.AbortChecked }
        IntegerDelegate { aspect: root.aspects.AbortAfter }
    }

    RowLayout {
        BoolDelegate { aspect: root.aspects.SamplesChecked }
        IntegerDelegate { aspect: root.aspects.BenchSamples }
    }

    RowLayout {
        BoolDelegate { aspect: root.aspects.ResamplesChecked }
        IntegerDelegate { aspect: root.aspects.BenchResamples }
    }

    RowLayout {
        BoolDelegate { aspect: root.aspects.ConfIntChecked }
        DoubleDelegate { aspect: root.aspects.BenchConfInt }
    }

    RowLayout {
        BoolDelegate { aspect: root.aspects.WarmupChecked }
        IntegerDelegate { aspect: root.aspects.BenchWarmup }
    }

    BoolDelegate { aspect: root.aspects.NoAnalysis }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    TableDelegate { aspect: aspects.Events }
    ButtonDelegate { aspect: aspects.ResetToDefaults }
    SelectionDelegate { aspect: aspects.CallgraphMode }
    IntegerDelegate { aspect: aspects.StackSize }
    SelectionDelegate { aspect: aspects.SampleMode }
    IntegerDelegate { aspect: aspects.Frequency }
    StringDelegate { aspect: aspects.ExtraArguments }
}

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

    contentFillsHeight: true
    TableDelegate { aspect: root.aspects.Events }
    ButtonDelegate { aspect: root.aspects.ResetToDefaults }
    SelectionDelegate { aspect: root.aspects.CallgraphMode }
    IntegerDelegate { aspect: root.aspects.StackSize }
    SelectionDelegate { aspect: root.aspects.SampleMode }
    IntegerDelegate { aspect: root.aspects.Frequency }
    StringDelegate { aspect: root.aspects.ExtraArguments }
}

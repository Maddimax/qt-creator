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

    StringDelegate { aspect: root.aspects.Host }
    StringDelegate { aspect: root.aspects.User }
    StringDelegate { aspect: root.aspects.Ssh }
    StringDelegate { aspect: root.aspects.Curl }
    IntegerDelegate { aspect: root.aspects.Port }
    BoolDelegate { aspect: root.aspects.Https }
}

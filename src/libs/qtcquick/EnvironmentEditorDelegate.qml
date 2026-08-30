// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts

// An aspect that is a set of environment changes, drawn wherever one is
// registered. The aspect is a container; what it holds is read by name.
EnvironmentEditor {
    required property Aspect aspect

    editor: AspectModels.named(aspect)
    visible: aspect?.visible ?? true
    Layout.fillWidth: true
    Layout.fillHeight: true
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// The custom widgets the plugin is built from: the list of classes, and what
// the current one is called and generates.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Intro }

    AspectListDelegate {
        objectName: "widgetClassList"
        aspect: root.aspects.Classes
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The form the code style test languages name. It stands in for a language
// that has moved to Qt Quick, so that a page built the way the Preferences
// dialog builds one has something to render.
AspectPage {
    id: root

    // Drawn generically rather than aspect by aspect: what the test factories
    // hand over differs, and the one that exercises style pools hands over
    // nothing at all. No style selector: its buttons are Qt Quick Controls,
    // and a plugin test loads Controls before a style is set, so drawing them
    // here would only add customization warnings to every run.
    AspectItems {
        Layout.fillWidth: true
        model: root.aspects.Settings ? AspectModels.container(root.aspects.Settings) : null
    }
}

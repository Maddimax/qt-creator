// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// What to pass to sdkmanager, with what it says about itself underneath. The
// help arrives after the dialog is open - sdkmanager is asked when it opens -
// so the field below starts empty and fills in.
AspectPage {
    id: root

    contentFillsHeight: true

    StringDelegate { aspect: root.aspects.Arguments }
    TextAreaDelegate { aspect: root.aspects.Help }
}

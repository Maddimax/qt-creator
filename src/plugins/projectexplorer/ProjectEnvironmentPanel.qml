// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The environment a project adds. What editing one looks like is
// EnvironmentEditor's business, and every page that edits an environment uses
// the same one.
AspectPage {
    id: root

    contentFillsHeight: true

    EnvironmentEditor {
        editor: AspectModels.named(root.aspects.Editor)
        Layout.fillHeight: true
        Layout.fillWidth: true
    }
}

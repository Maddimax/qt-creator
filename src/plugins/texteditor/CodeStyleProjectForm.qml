// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// One language's code style for one project: which style it uses, and a live
// preview. The same selector and preview the global page shows, without the
// language's own settings - a project picks a style rather than editing one.
ColumnLayout {
    id: root

    // A NamedAspects for the language's project container.
    required property var aspects

    spacing: Spacing.GapVS
    Layout.fillWidth: true
    Layout.fillHeight: true

    CodeStyleSelector { aspects: root.aspects }

    TextDisplayDelegate { aspect: root.aspects.ImmediateNote }

    CodeStylePreview { aspects: root.aspects }
}

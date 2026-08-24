// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Which code style a Code Style page is editing, and what can be done to it.
// These are the page's own aspects, not the language's, so every language gets
// the same selector; see CodeStyleAspect::setupSelectorAspects().
ColumnLayout {
    id: root

    // The page's NamedAspects.
    required property var aspects

    spacing: Spacing.GapVS
    Layout.fillWidth: true

    SelectionDelegate { aspect: root.aspects.Style }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        // The label column the delegates above reserve, so that the buttons
        // line up under the combo box rather than under its label.
        Item { Layout.preferredWidth: Metrics.formLabelWidth }

        ButtonDelegate { aspect: root.aspects.CopyStyle; Layout.fillWidth: false }
        ButtonDelegate { aspect: root.aspects.RemoveStyle; Layout.fillWidth: false }
        ButtonDelegate { aspect: root.aspects.ExportStyle; Layout.fillWidth: false }
        ButtonDelegate { aspect: root.aspects.ImportStyle; Layout.fillWidth: false }

        Item { Layout.fillWidth: true }
    }

    TextDisplayDelegate { aspect: root.aspects.ReadOnlyNote }
}

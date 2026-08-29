// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The named configurations of a beautifier tool, which all three of them show
// the same way. See Beautifier::Internal::ConfigurationsAspect.
ColumnLayout {
    id: root

    // The aspects of the ConfigurationsAspect, by name.
    required property var configurations

    spacing: Spacing.GapVS
    Layout.fillWidth: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        SelectionDelegate { aspect: root.configurations.Current }

        ButtonDelegate {
            aspect: root.configurations.Add
            Layout.fillWidth: false
        }

        ButtonDelegate {
            aspect: root.configurations.Remove
            Layout.fillWidth: false
        }
    }

    StringDelegate { aspect: root.configurations.Name }

    TextAreaDelegate {
        id: editor

        aspect: root.configurations.Value
        Layout.fillHeight: true

        // Where the cursor is is the view's business, and only the settings
        // know what the option under it means.
        onCurrentWordChanged: root.configurations.showDocumentationFor(currentWord)
    }

    TextDisplayDelegate { aspect: root.configurations.Documentation }

    // With no configuration chosen there is no editor to show, and nothing
    // else here grows - so the group's spare height had nowhere to go and
    // pushed what little there is to the bottom of it. This takes the space
    // when the editor is not there to take it.
    Item {
        visible: !editor.visible
        Layout.fillHeight: true
    }
}

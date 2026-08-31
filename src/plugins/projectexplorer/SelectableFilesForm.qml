// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Where to look for files, what to keep, and the tree of what was found. The
// directory and the button that reads it are hidden where the caller has
// already decided which directory this is about.
AspectPage {
    id: root

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        StringDelegate {
            aspect: root.aspects.BaseDir
            Layout.fillWidth: true
        }

        ButtonDelegate { aspect: root.aspects.StartParsing }
    }

    StringDelegate { aspect: root.aspects.SelectFilter }
    StringDelegate { aspect: root.aspects.HideFilter }

    // At the right, under the two filters it applies, which is where the
    // widget form's grid put it.
    RowLayout {
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }
        ButtonDelegate { aspect: root.aspects.ApplyFilters }
    }

    TreeDelegate {
        objectName: "selectableFilesTree"
        aspect: root.aspects.Files
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    TextDisplayDelegate { aspect: root.aspects.Preserved }
    TextDisplayDelegate { aspect: root.aspects.Progress }
}

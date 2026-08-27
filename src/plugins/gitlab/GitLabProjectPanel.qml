// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// GitLab for one project. Which of the buttons can be pressed, and whether the
// message above them is there at all, is the container's answer: it depends on
// whether this is a git repository, whether any server is configured, and
// whether the project is linked already.
AspectPage {
    id: root

    TextDisplayDelegate { aspect: root.aspects.GlobalLink }

    SelectionDelegate { aspect: root.aspects.Host }
    SelectionDelegate { aspect: root.aspects.LinkedServer }

    TextDisplayDelegate { aspect: root.aspects.Info }

    RowLayout {
        spacing: Spacing.GapHM

        ButtonDelegate { aspect: root.aspects.LinkWithGitLab }
        ButtonDelegate { aspect: root.aspects.Unlink }
        ButtonDelegate { aspect: root.aspects.CheckConnection }
        Item { Layout.fillWidth: true }
    }

    TextDisplayDelegate { aspect: root.aspects.Note }
}

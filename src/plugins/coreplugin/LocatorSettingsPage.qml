// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    contentFillsHeight: true

    // The Locator's own settings, shown here rather than owned: see
    // Utils::ContainerAspect.
    readonly property var settings: AspectModels.named(aspects.Settings.container)

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TreeDelegate {
            aspect: root.aspects.Filters
            Layout.fillHeight: true

            // Which filter the buttons act on. Where the cursor is is the
            // view's business; what may be done to what it is on is the
            // aspect's.
            onCurrentIndexChanged: root.aspects.Filters.setCurrentIndex(currentIndex)
        }

        // What a new filter is made of is a dialog's business, and there are
        // two kinds of them.
        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.AddDirectory; Layout.fillWidth: false }
            ButtonDelegate { aspect: root.aspects.AddUrl; Layout.fillWidth: false }
            ButtonDelegate { aspect: root.aspects.Remove; Layout.fillWidth: false }
            ButtonDelegate { aspect: root.aspects.Edit; Layout.fillWidth: false }
        }
    }

    IntegerDelegate { aspect: root.settings.RefreshInterval }
    BoolDelegate { aspect: root.settings.RelativePaths }
}

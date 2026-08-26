// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The file naming settings themselves, without a page around them: the global
// page and a project's panel show the same things and differ only in whose
// container they are shown for.
ColumnLayout {
    id: root

    // A NamedAspects for whichever container is being shown.
    required property var aspects

    spacing: Spacing.GapVS
    Layout.fillWidth: true

    AspectGroupBox {
        title: qsTr("Headers")

        ColumnLayout {
            SelectionDelegate { aspect: root.aspects.HeaderSuffix }
            StringDelegate { aspect: root.aspects.HeaderSearchPaths }
            StringDelegate { aspect: root.aspects.HeaderPrefixes }

            RowLayout {
                TextDisplayDelegate { aspect: root.aspects.IncludeGuardLabel }
                BoolDelegate { aspect: root.aspects.HeaderPragmaOnce }
                StringDelegate { aspect: root.aspects.HeaderGuardTemplate }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Sources")

        ColumnLayout {
            SelectionDelegate { aspect: root.aspects.SourceSuffix }
            StringDelegate { aspect: root.aspects.SourceSearchPaths }
            StringDelegate { aspect: root.aspects.SourcePrefixes }
        }
    }

    BoolDelegate { aspect: root.aspects.LowerCaseFiles }

    RowLayout {
        StringDelegate { aspect: root.aspects.LicenseTemplate }
        ButtonDelegate { aspect: root.aspects.EditLicenseTemplate }
    }
}

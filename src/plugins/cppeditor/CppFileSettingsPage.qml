// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    AspectGroupBox {
        title: qsTr("Headers")

        ColumnLayout {
            SelectionDelegate { aspect: aspects.HeaderSuffix }
            StringDelegate { aspect: aspects.HeaderSearchPaths }
            StringDelegate { aspect: aspects.HeaderPrefixes }

            RowLayout {
                TextDisplayDelegate { aspect: aspects.IncludeGuardLabel }
                BoolDelegate { aspect: aspects.HeaderPragmaOnce }
                StringDelegate { aspect: aspects.HeaderGuardTemplate }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Sources")

        ColumnLayout {
            SelectionDelegate { aspect: aspects.SourceSuffix }
            StringDelegate { aspect: aspects.SourceSearchPaths }
            StringDelegate { aspect: aspects.SourcePrefixes }
        }
    }

    BoolDelegate { aspect: aspects.LowerCaseFiles }

    RowLayout {
        StringDelegate { aspect: aspects.LicenseTemplate }
        ButtonDelegate { aspect: aspects.EditLicenseTemplate }
    }
}

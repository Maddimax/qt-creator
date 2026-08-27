// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("Configuration")

        ColumnLayout {
            StringDelegate { aspect: root.aspects.BinaryPath }
            StringDelegate { aspect: root.aspects.Path }
            BoolDelegate { aspect: root.aspects.WinSetHomeEnvironment }
        }
    }

    AspectGroupBox {
        title: qsTr("Miscellaneous")

        ColumnLayout {
            RowLayout {
                IntegerDelegate { aspect: root.aspects.LogCount }
                IntegerDelegate { aspect: root.aspects.Timeout }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Spacing.GapHM

                BoolDelegate { aspect: root.aspects.PullRebase }
                BoolDelegate { aspect: root.aspects.RebaseMerges }
                BoolDelegate { aspect: root.aspects.UpdateRefs }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Gitk")

        StringDelegate { aspect: root.aspects.GitKOptions }
    }

    AspectGroupBox {
        title: qsTr("Repository Browser")

        StringDelegate { aspect: root.aspects.RepositoryBrowserCmd }
    }

    AspectGroupBox {
        title: qsTr("Instant Blame")
        checkAspect: root.aspects.InstantBlame

        Flow {
            spacing: Spacing.GapHM

            BoolDelegate { aspect: root.aspects.GitInstantIgnoreSpaceChanges }
            BoolDelegate { aspect: root.aspects.GitInstantIgnoreLineMoves }
            BoolDelegate { aspect: root.aspects.GitInstantShowSubject }
        }
    }
}

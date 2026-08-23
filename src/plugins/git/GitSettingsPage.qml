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
        title: qsTr("Configuration")

        ColumnLayout {
            StringDelegate { aspect: aspects.BinaryPath }
            StringDelegate { aspect: aspects.Path }
            BoolDelegate { aspect: aspects.WinSetHomeEnvironment }
        }
    }

    AspectGroupBox {
        title: qsTr("Miscellaneous")

        ColumnLayout {
            RowLayout {
                IntegerDelegate { aspect: aspects.LogCount }
                IntegerDelegate { aspect: aspects.Timeout }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Spacing.GapHM

                BoolDelegate { aspect: aspects.PullRebase }
                BoolDelegate { aspect: aspects.RebaseMerges }
                BoolDelegate { aspect: aspects.UpdateRefs }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Gitk")

        StringDelegate { aspect: aspects.GitKOptions }
    }

    AspectGroupBox {
        title: qsTr("Repository Browser")

        StringDelegate { aspect: aspects.RepositoryBrowserCmd }
    }

    AspectGroupBox {
        title: qsTr("Instant Blame")
        checkAspect: aspects.InstantBlame

        Flow {
            spacing: Spacing.GapHM

            BoolDelegate { aspect: aspects.GitInstantIgnoreSpaceChanges }
            BoolDelegate { aspect: aspects.GitInstantIgnoreLineMoves }
            BoolDelegate { aspect: aspects.GitInstantShowSubject }
        }
    }
}

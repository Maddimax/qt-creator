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

    contentFillsHeight: true
    AspectGroupBox {
        title: qsTr("General")

        ColumnLayout {
            IntegerDelegate { aspect: root.aspects.ScanThreadLimit }
            BoolDelegate { aspect: root.aspects.OmitInternal }
            BoolDelegate { aspect: root.aspects.OmitRCWarnings }
            BoolDelegate { aspect: root.aspects.LimitResultOutput }

            // The check box is the field's label, the way the closure put them
            // side by side.
            RowLayout {
                BoolDelegate { aspect: root.aspects.LimitResultDescription }
                IntegerDelegate { aspect: root.aspects.ResultDescriptionMaxSize }
            }

            BoolDelegate { aspect: root.aspects.PopupOnStart }
            BoolDelegate { aspect: root.aspects.PopupOnFinish }
            BoolDelegate { aspect: root.aspects.PopupOnFail }
            BoolDelegate { aspect: root.aspects.AutoScrollResults }
            BoolDelegate { aspect: root.aspects.DisplayApp }
            BoolDelegate { aspect: root.aspects.ProcessArgs }
            SelectionDelegate { aspect: root.aspects.RunAfterBuild }

            RowLayout {
                BoolDelegate { aspect: root.aspects.UseTimeout }
                IntegerDelegate { aspect: root.aspects.Timeout }
            }

            ButtonDelegate { aspect: root.aspects.ResetChoiceCache }
        }
    }

    AspectGroupBox {
        title: qsTr("Active Test Frameworks")

        ColumnLayout {
            TableDelegate { aspect: root.aspects.Frameworks }
            TextDisplayDelegate { aspect: root.aspects.FrameworksWarning }
        }
    }
}

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
        title: qsTr("General")

        ColumnLayout {
            IntegerDelegate { aspect: aspects.ScanThreadLimit }
            BoolDelegate { aspect: aspects.OmitInternal }
            BoolDelegate { aspect: aspects.OmitRCWarnings }
            BoolDelegate { aspect: aspects.LimitResultOutput }

            // The check box is the field's label, the way the closure put them
            // side by side.
            RowLayout {
                BoolDelegate { aspect: aspects.LimitResultDescription }
                IntegerDelegate { aspect: aspects.ResultDescriptionMaxSize }
            }

            BoolDelegate { aspect: aspects.PopupOnStart }
            BoolDelegate { aspect: aspects.PopupOnFinish }
            BoolDelegate { aspect: aspects.PopupOnFail }
            BoolDelegate { aspect: aspects.AutoScrollResults }
            BoolDelegate { aspect: aspects.DisplayApp }
            BoolDelegate { aspect: aspects.ProcessArgs }
            SelectionDelegate { aspect: aspects.RunAfterBuild }

            RowLayout {
                BoolDelegate { aspect: aspects.UseTimeout }
                IntegerDelegate { aspect: aspects.Timeout }
            }

            ButtonDelegate { aspect: aspects.ResetChoiceCache }
        }
    }

    AspectGroupBox {
        title: qsTr("Active Test Frameworks")

        ColumnLayout {
            TableDelegate { aspect: aspects.Frameworks }
            TextDisplayDelegate { aspect: aspects.FrameworksWarning }
        }
    }
}

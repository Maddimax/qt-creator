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
            BoolDelegate { aspect: aspects.InterpretAmbiguousHeadersAsCHeaders }
            BoolDelegate { aspect: aspects.PCHUsage }
            BoolDelegate { aspect: aspects.UseBuiltinPreprocessor }
            BoolDelegate { aspect: aspects.EnableIndexing }

            RowLayout {
                BoolDelegate { aspect: aspects.SkipIndexingBigFiles }
                IntegerDelegate { aspect: aspects.IndexerFileSizeLimit }
            }

            RowLayout {
                BoolDelegate {
                    aspect: aspects.IgnoreFiles
                    Layout.alignment: Qt.AlignTop
                }
                TextAreaDelegate { aspect: aspects.IgnorePattern }
            }
        }
    }
}

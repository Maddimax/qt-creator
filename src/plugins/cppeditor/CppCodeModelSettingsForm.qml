// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The code model settings themselves, without a page around them: the global
// page and a project's panel show the same things and differ only in whose
// container they are shown for.
AspectGroupBox {
    id: root

    // A NamedAspects for whichever container is being shown.
    required property var aspects

    title: qsTr("General")

    ColumnLayout {
        BoolDelegate { aspect: root.aspects.InterpretAmbiguousHeadersAsCHeaders }
        BoolDelegate { aspect: root.aspects.PCHUsage }
        BoolDelegate { aspect: root.aspects.UseBuiltinPreprocessor }
        BoolDelegate { aspect: root.aspects.EnableIndexing }

        RowLayout {
            BoolDelegate { aspect: root.aspects.SkipIndexingBigFiles }
            IntegerDelegate { aspect: root.aspects.IndexerFileSizeLimit; compact: true }
        }

        RowLayout {
            BoolDelegate {
                aspect: root.aspects.IgnoreFiles
                Layout.alignment: Qt.AlignTop
            }
            TextAreaDelegate { aspect: root.aspects.IgnorePattern }
        }
    }
}

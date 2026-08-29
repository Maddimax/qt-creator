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

    AspectGroupBox {
        title: qsTr("Keywords")
        Layout.fillHeight: true

        AspectListDelegate { aspect: root.aspects.Keywords }
    }

    // No group around it: the aspect is called "Scanning Scope" and the
    // delegate draws that, so a group of the same name said it twice.
    RadioGroupDelegate { aspect: root.aspects.ScanningScope }
}

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
        title: qsTr("Note")

        TextDisplayDelegate { aspect: root.aspects.Note }
    }

    AspectGroupBox {
        title: qsTr("Use External Repository")
        checkAspect: root.aspects.UseExternalRepo

        StringListEditorDelegate { aspect: root.aspects.RepositoryUrls }
    }

    ButtonDelegate { aspect: root.aspects.InstallExtension }
}

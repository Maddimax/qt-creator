// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    // Why the page has nothing on it, when it has nothing on it.
    TextDisplayDelegate { aspect: root.aspects.Status }

    GroupDelegate { aspect: root.aspects.SdkGroup }
    GroupDelegate { aspect: root.aspects.TargetsGroup }

    // What the selected target asks for. Which packages those are changes
    // with the target, so the page names none of them.
    GroupDelegate { aspect: root.aspects.PackagesGroup }
    GroupDelegate { aspect: root.aspects.OptionalPackagesGroup }

    TextDisplayDelegate { aspect: root.aspects.TargetsInfo }
    BoolDelegate { aspect: root.aspects.AutomaticKitCreation }

    // The message and the two buttons read as one line; the group says so
    // with setInlineRow().
    GroupDelegate { aspect: root.aspects.KitGroup }
}

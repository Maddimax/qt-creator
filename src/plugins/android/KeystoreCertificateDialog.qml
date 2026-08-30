// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Creating a keystore and the certificate inside it. Three groups, and one
// line under them saying the first thing that is wrong with the lot - which
// the container works out, so the page only has to show it.
//
// The "Show password" boxes the widget dialog had are gone: a masked field
// offers to show itself now.
AspectPage {
    id: root

    GroupDelegate { aspect: root.aspects.Keystore }
    GroupDelegate { aspect: root.aspects.Certificate }
    GroupDelegate { aspect: root.aspects.Names }

    TextDisplayDelegate { aspect: root.aspects.Issue }
}

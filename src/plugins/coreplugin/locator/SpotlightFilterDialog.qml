// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// The external tool a file search is handed to, and what it is asked. The
// arguments are written in terms of the query variables, which the fields
// offer.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Command }
    StringDelegate { aspect: root.aspects.Arguments }
    StringDelegate { aspect: root.aspects.CaseSensitiveArguments }
    BoolWithOwnLabelDelegate { aspect: root.aspects.SortResults }

    LocatorFilterPrefixRow { aspects: root.aspects }
}

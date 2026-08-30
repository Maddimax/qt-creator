// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// What to paste and where. Either a list of diff chunks with a preview of the
// lot, or one block of text - which of the two is showing is the dialog's to
// say, so nothing here chooses.
AspectPage {
    id: root

    contentFillsHeight: true

    SelectionDelegate { aspect: root.aspects.Protocol }
    IntegerDelegate { aspect: root.aspects.Expiry }
    StringDelegate { aspect: root.aspects.Username }
    StringDelegate { aspect: root.aspects.Description }

    TableDelegate { aspect: root.aspects.Parts }
    TextAreaDelegate { aspect: root.aspects.Preview }
    TextAreaDelegate { aspect: root.aspects.PlainText }
}

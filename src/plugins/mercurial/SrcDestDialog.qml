// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Where a pull or push goes. The repository hg already knows about is shown
// under the answer that means it; which field can be typed into follows the
// answer, and that is the aspects' own doing.
AspectPage {
    id: root

    RadioGroupDelegate { aspect: root.aspects.Location }

    TextDisplayDelegate { aspect: root.aspects.DefaultLocation }
    BoolDelegate { aspect: root.aspects.PromptForCredentials }

    StringDelegate { aspect: root.aspects.LocalPath }
    StringDelegate { aspect: root.aspects.Url }
}

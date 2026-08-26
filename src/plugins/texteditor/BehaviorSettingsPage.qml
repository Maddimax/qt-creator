// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Five settings objects share this page, so their aspects are reached through
// AspectModels.named() rather than through the page's own "aspects". Each of
// the five is a form of its own, because a project's Editor panel shows the
// same five.
AspectPage {
    id: root

    TabSettingsForm { aspects: AspectModels.named(root.aspects.Tabs) }
    TypingSettingsForm { aspects: AspectModels.named(root.aspects.Typing) }
    StorageSettingsForm { aspects: AspectModels.named(root.aspects.Storage) }
    EncodingSettingsForm { aspects: AspectModels.named(root.aspects.Encoding) }
    BehaviorSettingsForm { aspects: AspectModels.named(root.aspects.Behavior) }
}

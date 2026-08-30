// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// What is known about a file. Everything above the permissions is read back
// from the file and cannot be typed into; the three permissions can, and
// writing one re-reads all of them.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Name }
    StringDelegate { aspect: root.aspects.Path }
    StringDelegate { aspect: root.aspects.MimeType }
    StringDelegate { aspect: root.aspects.DefaultEditor }
    StringDelegate { aspect: root.aspects.LineEndings }
    StringDelegate { aspect: root.aspects.Indentation }
    StringDelegate { aspect: root.aspects.Owner }
    StringDelegate { aspect: root.aspects.Group }
    StringDelegate { aspect: root.aspects.Size }
    StringDelegate { aspect: root.aspects.LastRead }
    StringDelegate { aspect: root.aspects.LastModified }
    StringDelegate { aspect: root.aspects.VcsStatus }

    BoolWithOwnLabelDelegate { aspect: root.aspects.Readable }
    BoolWithOwnLabelDelegate { aspect: root.aspects.Writable }
    BoolWithOwnLabelDelegate { aspect: root.aspects.Executable }
    BoolWithOwnLabelDelegate { aspect: root.aspects.SymLink }
}

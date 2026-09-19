// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcseditordocument.h>

#include <functional>

namespace Utils { class FilePath; }

namespace Perforce::Internal {

// The change under \a cursor: any word that is a number.
QString perforceChangeUnderCursor(const QTextCursor &cursor);

// The parameters of a Perforce editor of \a type: the patterns and texts the
// widget subclass declared, its annotation highlighter, the change under the
// cursor, a change list's predecessor, and the depot path a diff names mapped
// to the file on disk, with the describe function the caller supplies.
VcsBase::VcsBaseEditorParameters perforceEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe);

} // Perforce::Internal

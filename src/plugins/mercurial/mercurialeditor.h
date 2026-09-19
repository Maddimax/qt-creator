// Copyright (C) 2016 Brian McGillion
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcseditordocument.h>

#include <functional>

namespace Utils { class FilePath; }

namespace Mercurial::Internal {

// The change under \a cursor: a word of exactly twelve or forty hex digits.
QString mercurialChangeUnderCursor(const QTextCursor &cursor);

// The parameters of a Mercurial editor of \a type: the patterns and texts
// the widget subclass declared, its annotation highlighter, and what it
// answered about a change - the one under the cursor, its short description,
// its parents - with the describe function the caller supplies.
VcsBase::VcsBaseEditorParameters mercurialEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe);

} // Mercurial::Internal

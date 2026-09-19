// Copyright (c) 2018 Artur Shepilko
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcseditordocument.h>

#include <functional>

namespace Utils { class FilePath; }

namespace Fossil::Internal {

// The change under \a cursor: a word of five to forty hex digits.
QString fossilChangeUnderCursor(const QTextCursor &cursor);

// The parameters of a Fossil editor of \a type: the patterns and texts the
// widget subclass declared, its annotation highlighter, and what it answered
// about a change - the one under the cursor, its committer and comment, its
// parents - with the describe function the caller supplies.
VcsBase::VcsBaseEditorParameters fossilEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe);

} // Fossil::Internal

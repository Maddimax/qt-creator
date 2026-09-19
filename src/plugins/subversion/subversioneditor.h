// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcseditordocument.h>

#include <functional>

namespace Utils { class FilePath; }

namespace Subversion::Internal {

// The change under \a cursor: the number at the start of an annotation line,
// or an "r123" / "revision 123" the cursor is inside of in a log.
QString subversionChangeUnderCursor(const QTextCursor &cursor);

// The parameters of a Subversion editor of \a type: the patterns and texts
// the widget subclass declared, its annotation highlighter, the change under
// the cursor and a revision's predecessor, with the describe function the
// caller supplies.
VcsBase::VcsBaseEditorParameters subversionEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe);

} // namespace Subversion::Internal

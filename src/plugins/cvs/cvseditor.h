// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcseditordocument.h>

#include <functional>

namespace Utils { class FilePath; }

namespace Cvs::Internal {

// The change under \a cursor in output of \a type: a revision ("1.1") at the
// start of an annotation line, or past "revision" in a log; nothing elsewhere.
QString cvsChangeUnderCursor(VcsBase::EditorContentType type, const QTextCursor &cursor);

// The parameters of a CVS editor of \a type: the patterns and texts the
// widget subclass declared, its annotation highlighter, the change under the
// cursor and a revision's predecessor, with the describe function the caller
// supplies.
VcsBase::VcsBaseEditorParameters cvsEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe);

} // Cvs::Internal

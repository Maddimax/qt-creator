// Copyright (C) 2016 Hugues Delorme
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcseditordocument.h>

#include <functional>

namespace Utils { class FilePath; }

namespace Bazaar::Internal {

// The change under \a cursor: a revision number, on a line that names one
// - a number alone matches too much of a log to be one anywhere.
QString bazaarChangeUnderCursor(const QTextCursor &cursor);

// The parameters of a Bazaar editor of \a type: the patterns and texts the
// widget subclass declared, its annotation highlighter and the change under
// the cursor, with the describe function the caller supplies.
VcsBase::VcsBaseEditorParameters bazaarEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe);

} // Bazaar::Internal

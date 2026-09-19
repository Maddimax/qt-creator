// Copyright (C) 2016 AudioCodes Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcseditordocument.h>

#include <functional>

namespace Utils { class FilePath; }

namespace ClearCase::Internal {

// The change under \a cursor: a version path ("/main/branch/3") anywhere on
// its line, in a log's "create version" lines and a blame's entries alike.
QString clearCaseChangeUnderCursor(const QTextCursor &cursor);

// The parameters of a ClearCase editor of \a type: the patterns and texts
// the widget subclass declared, its annotation highlighter and the change
// under the cursor, with the describe function the caller supplies.
VcsBase::VcsBaseEditorParameters clearCaseEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe);

} // ClearCase::Internal

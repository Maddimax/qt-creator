// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

namespace Utils { class MultiTextCursor; }

namespace TextEditor {

// Editor commands that are nothing but a transformation of where the carets
// are and what is under them. They belong to neither editor: the widget one
// and the Quick one both have to answer the same menu entry, and an operation
// written against a widget can only be answered by one of them.

// Pulls the line after each caret onto the caret's own, with the leading
// whitespace of what arrives collapsed to a single space. A caret with a
// selection joins every line the selection touches.
TEXTEDITOR_EXPORT void joinLines(Utils::MultiTextCursor &cursor);

} // namespace TextEditor

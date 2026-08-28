// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <functional>

QT_BEGIN_NAMESPACE
class QString;
QT_END_NAMESPACE

namespace Utils { class MultiTextCursor; }

namespace TextEditor {

class TextDocument;

// Editor commands that are nothing but a transformation of where the carets
// are and what is under them. They belong to neither editor: the widget one
// and the Quick one both have to answer the same menu entry, and an operation
// written against a widget can only be answered by one of them.

// Pulls the line after each caret onto the caret's own, with the leading
// whitespace of what arrives collapsed to a single space. A caret with a
// selection joins every line the selection touches.
TEXTEDITOR_EXPORT void joinLines(Utils::MultiTextCursor &cursor);

// Replaces what each caret has selected with \a transform of it, and selects
// the result again. A caret with no selection takes the word it is in - but
// only when it is the only caret, because several carets each swallowing a
// word is rarely what was meant by pressing the key once.
using TextTransformation = std::function<QString(const QString &)>;
TEXTEDITOR_EXPORT void transformSelection(Utils::MultiTextCursor &cursor,
                                          const TextTransformation &transform);

// Opens a line above or below each caret's own and leaves the caret on it,
// indented the way the document would indent it.
TEXTEDITOR_EXPORT void insertLineAbove(Utils::MultiTextCursor &cursor, TextDocument *document);
TEXTEDITOR_EXPORT void insertLineBelow(Utils::MultiTextCursor &cursor, TextDocument *document);

} // namespace TextEditor

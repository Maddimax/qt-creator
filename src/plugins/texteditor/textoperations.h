// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <functional>

QT_BEGIN_NAMESPACE
class QString;
QT_END_NAMESPACE

namespace Utils {
class CommentDefinition;
class MultiTextCursor;
}

QT_BEGIN_NAMESPACE
class QTextCursor;
class QTextDocument;
QT_END_NAMESPACE

namespace TextEditor {

class TabSettingsData;
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

// Puts a second copy of what each caret has selected after it, and selects
// the copy. A caret with no selection duplicates its whole line - but only
// when it is the only caret, for the same reason the case commands take a
// word only then. \a comment, when given, wraps the copy in that language's
// comment markers, which is what "Duplicate and Comment" is.
TEXTEDITOR_EXPORT void duplicateSelection(Utils::MultiTextCursor &cursor,
                                          const Utils::CommentDefinition *comment = nullptr);

// Sorts the selected lines. With nothing selected it takes the run of lines
// around the caret that share its indentation, which is what makes the
// command useful without selecting first. False when there was nothing to
// sort, so that the caller leaves the cursor where it was.
TEXTEDITOR_EXPORT bool sortLines(QTextCursor &cursor, const TabSettingsData &tabSettings);

// Grows each caret that has selected nothing to cover its whole line, newline
// and all, so that the line commands have something to work on. Carets that
// have a selection are left as they are: the reader already said what to act
// on.
TEXTEDITOR_EXPORT void maybeSelectLine(Utils::MultiTextCursor &cursor, QTextDocument *document);

// Puts a copy of the line, or of the selected lines, above or below them, and
// leaves the caret on the copy so that it can be edited straight away.
TEXTEDITOR_EXPORT void copyLineUpDown(QTextCursor &cursor, bool up, TextDocument *document);

// The whole lines that \a cursor covers, as a cursor selecting them. Separate
// from the move below so that a caller can look at what is about to move
// before it does - the widget editor's refactor markers have to be measured
// against the old text, because removing it takes their positions with it.
TEXTEDITOR_EXPORT QTextCursor selectLinesToMove(const QTextCursor &cursor);

// Moves the lines \a move has selected one line up or down and leaves \a move
// on them, selected again when \a hadSelection. Re-indents them unless they
// are commented out, on the grounds that a comment's indentation is the
// reader's business rather than the indenter's. Returns where the moved text
// now starts.
// Reflows the paragraph the caret is in to \a paragraphWidth columns, keeping
// its indentation - or the prefix its lines share, which is what makes it work
// on a doxygen comment without eating the stars.
TEXTEDITOR_EXPORT void rewrapParagraph(QTextCursor &cursor, const TabSettingsData &ts,
                                       int paragraphWidth);

TEXTEDITOR_EXPORT int moveSelectedLines(QTextCursor &move, bool up, bool hadSelection,
                                        TextDocument *document,
                                        const Utils::CommentDefinition &comment);

} // namespace TextEditor

// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include "command.h"

#include <utils/filepath.h>
#include <utils/result.h>

namespace Utils { class PlainTextEdit; }

namespace Core { class IEditor; }

namespace TextEditor {

// Format whatever the reader is looking at. Which view that is does not
// matter: the text and the file name are the document's, not the view's.
TEXTEDITOR_EXPORT void formatCurrentFile(const TextEditor::Command &command,
                                         int startPos = -1, int endPos = 0);

// Format the file \a editor is showing with \a command, in either view. A
// negative \a startPos formats the whole file; otherwise only that range is
// handed to the formatter and only that range is replaced by the answer.
TEXTEDITOR_EXPORT void formatEditor(Core::IEditor *editor, const TextEditor::Command &command,
                                    int startPos = -1, int endPos = 0);

// The same without waiting for the formatter, and giving up if the document
// is edited before the answer arrives.
TEXTEDITOR_EXPORT void formatEditorAsync(Core::IEditor *editor, const TextEditor::Command &command,
                                         int startPos = -1, int endPos = 0);

TEXTEDITOR_EXPORT void updateEditorText(Utils::PlainTextEdit *editor, const QString &text);

// The same for whichever view \a editor has. Formatting rewrites the whole
// file, so the caret would otherwise jump: each view knows how far it has
// scrolled and puts the caret back where it was on the screen.
TEXTEDITOR_EXPORT void updateEditorText(Core::IEditor *editor, const QString &text);

// Runs \a command over \a text as if it were the contents of \a filePath, and
// returns what came back. For text that is not in an editor - a code style
// preview - and synchronous, so keep it to the small amounts that suits.
TEXTEDITOR_EXPORT Utils::Result<QString> formatText(const Utils::FilePath &filePath,
                                                    const QString &text,
                                                    const Command &command);

namespace Internal { QObject *createFormatTextTest(); }

} // namespace TextEditor

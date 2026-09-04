// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QMap>
#include <QString>
#include <QTextCursor>

namespace TextEditor {
class TextDocument;
class TextEditorWidget;
}

namespace Utils { class FilePath; }
namespace CPlusPlus { class Snapshot; }

namespace Debugger::Internal {

class ContextData;
class Location;

// Editor tooltip support
// The expression at \a pos, worked out from the document and the caret rather
// than from a view: everything this needs - the file, the character at a
// position, what is selected - is the document's, and asking a widget for it
// left the Qt Quick editor unable to answer.
QString cppExpressionAt(TextEditor::TextDocument *document, const QTextCursor &cursor, int pos,
                        int *line, int *column, QString *function = nullptr,
                        int *scopeFromLine = nullptr, int *scopeToLine = nullptr);
QString fixCppExpression(const QString &exp);
QString cppFunctionAt(const Utils::FilePath &filePath, int line, int column = 0);

// Get variables that are not initialized at a certain line
// of a function from the code model. Shadowed variables will
// be reported using the debugger naming conventions '<shadowed n>'
QStringList getUninitializedVariables(const CPlusPlus::Snapshot &snapshot,
                                      const QString &function, const Utils::FilePath &file, int line);

ContextData getLocationContext(TextEditor::TextDocument *document, int lineNumber);

// Where the reader is, in whichever view is showing the file. What
// BaseTextEditor::currentTextEditor() really filtered for is the current
// editor's document being a TextDocument, and Core::IEditor answers the line
// for the Qt Quick editor too. Invalid when no text file is current.
ContextData currentLocationContext();

} // Debugger::Internal

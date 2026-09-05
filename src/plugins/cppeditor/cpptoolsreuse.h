// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"

#include <cplusplus/CppDocument.h>
#include <texteditor/quickfix.h>
#include <utils/id.h>
#include <utils/filepath.h>

#include <QTextCursor>
#include <utils/searchresultitem.h>

namespace CPlusPlus {
class Macro;
class Symbol;
class LookupContext;
} // namespace CPlusPlus
namespace Core { class IEditor; }
namespace ProjectExplorer { class Project; }
namespace TextEditor {
class AssistInterface;
class TextDocument;
class TextEditorWidget;
}
namespace Utils { namespace Text { class Range; } }

namespace CppEditor {
class CppEditorDocument;
class CppEditorWidget;
class CppRefactoringFile;
class CursorInEditor;
class ProjectInfo;
class CppCompletionAssistProcessor;

enum class FollowSymbolMode { Exact, Fuzzy };

// Jump between a function's declaration and its definition, and from a virtual
// call to the implementation it would reach. What the view is asked for is the
// caret and somewhere to land, so any view answers - see
// TextEditor::textCursorOf() and TextEditor::openLinkInEditor().
void CPPEDITOR_EXPORT switchDeclarationDefinition(Core::IEditor *editor, bool inNextSplit);
void CPPEDITOR_EXPORT goToParentImpl(Core::IEditor *editor, bool inNextSplit);

// Where else the symbol under \a cursor is used, and renaming it everywhere.
// Both are the built-in code model's answers; a language server answers the
// same two questions itself. \a cursor defaults to where the caret is.
void CPPEDITOR_EXPORT findUsagesOf(Core::IEditor *editor, QTextCursor cursor = {});
void CPPEDITOR_EXPORT renameUsagesOf(Core::IEditor *editor,
                                     const QString &replacement = {},
                                     QTextCursor cursor = {});

// Says so in the info bar when \a filePath is generated, because renaming
// something declared there is overwritten by the next build.
void CPPEDITOR_EXPORT showRenameWarningIfFileIsGenerated(const Utils::FilePath &filePath);

// The editor \a widget is the view of. What gets from a widget-side object to
// the view-agnostic functions above, and to TextEditor::setViewSelections().
Core::IEditor CPPEDITOR_EXPORT *editorFor(TextEditor::TextEditorWidget *widget);

// The editor \a data was taken in - the view the reader asked from, which a
// CursorInEditor only names directly where that view is a widget.
Core::IEditor CPPEDITOR_EXPORT *editorFor(const CursorInEditor &data);

#ifdef WITH_TESTS
namespace Internal { QObject *createSymbolJumpTest(); }
#endif

void CPPEDITOR_EXPORT moveCursorToEndOfIdentifier(QTextCursor *tc);
void CPPEDITOR_EXPORT moveCursorToStartOfIdentifier(QTextCursor *tc);

bool CPPEDITOR_EXPORT isQtKeyword(QStringView text);

bool CPPEDITOR_EXPORT isValidAsciiIdentifierChar(const QChar &ch);
bool CPPEDITOR_EXPORT isValidFirstIdentifierChar(const QChar &ch);
bool CPPEDITOR_EXPORT isValidIdentifierChar(const QChar &ch);
bool CPPEDITOR_EXPORT isValidIdentifier(const QString &s);

int CPPEDITOR_EXPORT activeArgumentForPrefix(const QString &prefix);

QStringList CPPEDITOR_EXPORT identifierWordsUnderCursor(const QTextCursor &tc);
QString CPPEDITOR_EXPORT identifierUnderCursor(QTextCursor *cursor);

const CPlusPlus::Macro CPPEDITOR_EXPORT *findCanonicalMacro(const QTextCursor &cursor,
                                                           CPlusPlus::Document::Ptr document);

bool CPPEDITOR_EXPORT isInCommentOrString(const TextEditor::AssistInterface *interface,
                                          CPlusPlus::LanguageFeatures features);
bool CPPEDITOR_EXPORT isInCommentOrString(const QTextCursor &cursor,
                                          CPlusPlus::LanguageFeatures features);
TextEditor::QuickFixOperations CPPEDITOR_EXPORT
quickFixOperations(const TextEditor::AssistInterface *interface);

CppCompletionAssistProcessor CPPEDITOR_EXPORT *getCppCompletionAssistProcessor();

QString CPPEDITOR_EXPORT
deriveHeaderGuard(const Utils::FilePath &filePath, ProjectExplorer::Project *project);

enum class CacheUsage { ReadWrite, ReadOnly };

Utils::FilePath CPPEDITOR_EXPORT correspondingHeaderOrSource(
     const Utils::FilePath &filePath, bool *wasHeader = nullptr,
     CacheUsage cacheUsage = CacheUsage::ReadWrite);

void CPPEDITOR_EXPORT openEditor(const Utils::FilePath &filePath, bool inNextSplit,
                                 Utils::Id editorId = {});

QString CPPEDITOR_EXPORT preferredCxxHeaderSuffix(ProjectExplorer::Project *project);
QString CPPEDITOR_EXPORT preferredCxxSourceSuffix(ProjectExplorer::Project *project);
bool CPPEDITOR_EXPORT preferLowerCaseFileNames(ProjectExplorer::Project *project);

QList<Utils::Text::Range> CPPEDITOR_EXPORT symbolOccurrencesInText(
    const QTextDocument &doc, QStringView text, int offset, const QString &symbolName);
Utils::SearchResultItems CPPEDITOR_EXPORT
symbolOccurrencesInDeclarationComments(const Utils::SearchResultItems &symbolOccurrencesInCode);
// The document rather than a view: what this reads is the parse and the text.
QList<Utils::Text::Range> CPPEDITOR_EXPORT symbolOccurrencesInDeclarationComments(
    CppEditorDocument *document, const QTextCursor &cursor);

bool fileSizeExceedsLimit(const Utils::FilePath &filePath, int sizeLimitInMb);

namespace Internal {
void decorateCppDocument(TextEditor::TextDocument *document);
} // namespace Internal

} // CppEditor

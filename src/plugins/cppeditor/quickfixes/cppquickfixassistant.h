// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../cppsemanticinfo.h"

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/iassistprovider.h>
#include <texteditor/quickfix.h>

#include <cplusplus/LookupContext.h>

namespace CppEditor {
class CppEditorDocument;
class CppEditorWidget;
class CppRefactoringFile;
using CppRefactoringFilePtr = QSharedPointer<CppRefactoringFile>;

namespace Internal {

class CppQuickFixInterface : public TextEditor::AssistInterface
{
public:
    // Over the document, which is where the semantic info and the file being
    // changed both live. editor() is null here: a fix that needs a widget -
    // one that reads what the caret is doing - has to say so and step aside.
    CppQuickFixInterface(CppEditorDocument *document,
                         const QTextCursor &cursor,
                         TextEditor::AssistReason reason);
    CppQuickFixInterface(CppEditorWidget *editor, TextEditor::AssistReason reason);

    const QList<CPlusPlus::AST *> &path() const;
    CPlusPlus::Snapshot snapshot() const;
    SemanticInfo semanticInfo() const;
    const CPlusPlus::LookupContext &context() const;
    // The view, where there is one. A fix that needs it must check.
    CppEditorWidget *editor() const;
    // The file being changed, which there always is.
    CppEditorDocument *cppEditorDocument() const;

    CppRefactoringFilePtr currentFile() const;

    bool isCursorOn(unsigned tokenIndex) const;
    bool isCursorOn(const CPlusPlus::AST *ast) const;
    bool isBaseObject() const override { return false; }

private:
    QTextCursor adjustedCursor();

    CppEditorWidget *m_editor = nullptr;
    CppEditorDocument *m_document;
    SemanticInfo m_semanticInfo;
    CPlusPlus::Snapshot m_snapshot;
    CppRefactoringFilePtr m_currentFile;
    CPlusPlus::LookupContext m_context;
    QList<CPlusPlus::AST *> m_path;
};

TextEditor::IAssistProvider &cppQuickFixAssistProvider();

TextEditor::QuickFixOperations quickFixOperations(const TextEditor::AssistInterface *interface);

#ifdef WITH_TESTS
QObject *createQuickFixAssistTest();
#endif

} // Internal
} // CppEditor

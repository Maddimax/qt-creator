// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../cppsemanticinfo.h"

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/iassistprovider.h>
#include <texteditor/quickfix.h>

#include <cplusplus/LookupContext.h>

#include <QPointer>

namespace Core { class IEditor; }

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
    // changed both live. \a editor is the view that asked, which a fix acts on
    // when what it does ends in a caret - null where nobody in particular did.
    CppQuickFixInterface(CppEditorDocument *document,
                         const QTextCursor &cursor,
                         TextEditor::AssistReason reason,
                         Core::IEditor *editor = nullptr);

    const QList<CPlusPlus::AST *> &path() const;
    CPlusPlus::Snapshot snapshot() const;
    SemanticInfo semanticInfo() const;
    const CPlusPlus::LookupContext &context() const;
    // The view that asked, where one did. A fix that needs it must check: a
    // proposal can outlive the view, and a fix can be run by a test with no
    // view at all.
    Core::IEditor *editor() const;
    // The file being changed, which there always is.
    CppEditorDocument *cppEditorDocument() const;

    CppRefactoringFilePtr currentFile() const;

    bool isCursorOn(unsigned tokenIndex) const;
    bool isCursorOn(const CPlusPlus::AST *ast) const;
    bool isBaseObject() const override { return false; }

private:
    QTextCursor adjustedCursor();
    void findLocalUses(const QTextCursor &cursor);

    QPointer<Core::IEditor> m_editor;
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

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppquickfixassistant.h"

#include "../cppeditordocument.h"
#include "../cppeditorwidget.h"
#include "../cppmodelmanager.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/iassistprocessor.h>
#include <texteditor/textdocument.h>

#include <cplusplus/ASTPath.h>

#include <utils/qtcassert.h>

#include <QLoggingCategory>

#ifdef WITH_TESTS
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <texteditor/texteditor.h>
#include <utils/temporarydirectory.h>
#include <QScopeGuard>
#include <QTest>
#endif

using namespace CPlusPlus;
using namespace TextEditor;

namespace CppEditor::Internal {

static Q_LOGGING_CATEGORY(log, "qtc.cppeditor.quickfixes", QtWarningMsg)

// CppQuickFixAssistProvider

class CppQuickFixAssistProvider final : public IAssistProvider
{
public:
    class CppQuickFixAssistProcessor final : public IAssistProcessor
    {
        IAssistProposal *perform() final
        {
            return GenericProposal::createProposal(interface(), quickFixOperations(interface()));
        }
    };

    TextEditor::IAssistProcessor *createProcessor(const TextEditor::AssistInterface *) const final
    {
        return new CppQuickFixAssistProcessor;
    }
};

IAssistProvider &cppQuickFixAssistProvider()
{
    static CppQuickFixAssistProvider theCppQuickFixAssistProvider;
    return theCppQuickFixAssistProvider;
}

// CppQuickFixAssistInterface

CppQuickFixInterface::CppQuickFixInterface(CppEditorDocument *document,
                                           const QTextCursor &cursor,
                                           AssistReason reason)
    : AssistInterface(cursor, document->filePath(), reason)
    , m_document(document)
    , m_semanticInfo(document->semanticInfo())
    , m_snapshot(CppModelManager::snapshot())
    , m_currentFile(CppRefactoringChanges::file(document, m_semanticInfo.doc))
    , m_context(m_semanticInfo.doc, m_snapshot)
{
    QTC_CHECK(m_semanticInfo.doc);
    QTC_CHECK(m_semanticInfo.doc->translationUnit());
    QTC_CHECK(m_semanticInfo.doc->translationUnit()->ast());
    ASTPath astPath(m_semanticInfo.doc);
    m_path = astPath(adjustedCursor());
}

// The widget's own semantic info rather than the document's: the widget
// patches the local uses into it as the caret moves, and a fix that renames a
// local reads them.
CppQuickFixInterface::CppQuickFixInterface(CppEditorWidget *editor, AssistReason reason)
    : AssistInterface(editor->textCursor(), editor->textDocument()->filePath(), reason)
    , m_editor(editor)
    , m_document(editor->cppEditorDocument())
    , m_semanticInfo(editor->semanticInfo())
    , m_snapshot(CppModelManager::snapshot())
    , m_currentFile(CppRefactoringChanges::file(editor, m_semanticInfo.doc))
    , m_context(m_semanticInfo.doc, m_snapshot)
{
    QTC_CHECK(m_semanticInfo.doc);
    QTC_CHECK(m_semanticInfo.doc->translationUnit());
    QTC_CHECK(m_semanticInfo.doc->translationUnit()->ast());
    ASTPath astPath(m_semanticInfo.doc);
    m_path = astPath(adjustedCursor());
}

const QList<AST *> &CppQuickFixInterface::path() const
{
    return m_path;
}

Snapshot CppQuickFixInterface::snapshot() const
{
    return m_snapshot;
}

SemanticInfo CppQuickFixInterface::semanticInfo() const
{
    return m_semanticInfo;
}

const LookupContext &CppQuickFixInterface::context() const
{
    return m_context;
}

CppEditorWidget *CppQuickFixInterface::editor() const
{
    return m_editor;
}

CppEditorDocument *CppQuickFixInterface::cppEditorDocument() const
{
    return m_document;
}

CppRefactoringFilePtr CppQuickFixInterface::currentFile() const
{
    return m_currentFile;
}

bool CppQuickFixInterface::isCursorOn(unsigned tokenIndex) const
{
    return currentFile()->isCursorOn(tokenIndex);
}

bool CppQuickFixInterface::isCursorOn(const AST *ast) const
{
    return currentFile()->isCursorOn(ast);
}

// Some users like to select identifiers and expect the quickfix to apply to the selection.
// However, as the cursor position is at the end of the selection, it can happen that
// the quickfix is applied to the following token instead; see e.g. QTCREATORBUG-27886.
// We try to detect this condition: If there is a selection *and* this selection
// corresponds to a C++ token, we move the cursor to that token's position.
QTextCursor CppQuickFixInterface::adjustedCursor()
{
    QTextCursor cursor = this->cursor();
    if (!cursor.hasSelection())
        return cursor;

    const TranslationUnit * const tu = m_semanticInfo.doc->translationUnit();
    const int selStart = cursor.selectionStart();
    const int selEnd = cursor.selectionEnd();
    const QTextDocument * const doc = m_editor->textDocument()->document();

    // Binary search for matching token.
    for (int l = 0, u = tu->tokenCount() - 1; l <= u; ) {
        const int i = (l + u) / 2;
        const int tokenPos = tu->getTokenPositionInDocument(i, doc);
        if (selStart < tokenPos) {
            u = i - 1;
            continue;
        }
        if (selStart > tokenPos) {
            l = i + 1;
            continue;
        }

        // Selection does not end at token end.
        if (tokenPos + tu->tokenAt(i).utf16chars() != selEnd)
            break;

        cursor.setPosition(selStart);

        // Try not to have the cursor "at the edge", in order to prevent potential ambiguities.
        if (selEnd - selStart > 1)
            cursor.setPosition(cursor.position() + 1);

        return cursor;
    }
    return cursor;
}

QuickFixOperations quickFixOperations(const TextEditor::AssistInterface *interface)
{
    const auto cppInterface = dynamic_cast<const CppQuickFixInterface *>(interface);
    if (!cppInterface)
        return {};

    const SemanticInfo &info = cppInterface->semanticInfo();
    qCDebug(log) << "gathering quickfixes";
    QTC_ASSERT(cppInterface->cppEditorDocument(), return {});
    qCDebug(log) << "document revision:" << cppInterface->cppEditorDocument()->document()->revision();
    qCDebug(log) << "semantic info revision:" << info.revision;
    qCDebug(log) << "semantic info complete:" << info.complete;
    qCDebug(log) << "semantic info has valid snapshot:" << !info.snapshot.isEmpty();

    QuickFixOperations quickFixes;
    for (CppQuickFixFactory *factory : CppQuickFixFactory::cppQuickFixFactories())
        factory->match(*cppInterface, quickFixes);
    return quickFixes;
}


#ifdef WITH_TESTS

// Quick fixes are offered on the document, not on the widget showing it. The
// interface a C++ file needs carries the semantic info and the AST path, and
// CppEditorWidget was the only thing that could build one - so a C++ file in
// the Qt Quick editor was offered nothing at all.
class QuickFixAssistTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDocumentBuildsTheCppInterface()
    {
        Utils::TemporaryDirectory dir("cpp-quickfix-without-a-widget");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int main()\n{\n    int alpha = 1;\n    return alpha;\n}\n"));

        // The C++ factory, told to build the Quick view for the length of this
        // test whatever QTC_QUICK_CPP_EDITOR says.
        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(factory, "no editor factory claims a C++ file");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        // The premise: no widget, so nothing in the old path could have built
        // the interface.
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY2(document, "the Quick editor was not given a CppEditorDocument");

        document->recalculateSemanticInfo();
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the file says");

        QTextCursor cursor(document->document());
        cursor.setPosition(document->document()->findBlockByNumber(2).position()
                           + QString("    int al").size());

        const std::unique_ptr<TextEditor::AssistInterface> interface
            = document->createAssistInterface(cursor, TextEditor::QuickFix,
                                              TextEditor::ExplicitlyInvoked);
        QVERIFY2(dynamic_cast<CppQuickFixInterface *>(interface.get()),
                 "the document answered the base interface, which knows no C++");

        const TextEditor::QuickFixOperations operations = quickFixOperations(interface.get());
        QStringList offered;
        for (const TextEditor::QuickFixOperation::Ptr &operation : operations)
            offered << operation->description();
        // A named one rather than "not empty": every factory is asked, and a
        // list that happens to be non-empty says nothing about whether the
        // AST path this interface carries is the right one.
        QVERIFY2(offered.contains("Convert to Pointer"),
                 qPrintable("the caret is on 'alpha' and nothing offered to change it; got: "
                            + offered.join(", ")));
    }

    // The file a refactoring changes is looked up by path, and that lookup
    // preferred an editor "as these are already parsed and up to date with
    // regards to unsaved changes". It asked for a CppEditorWidget, so a file
    // open in the Quick editor fell through to the model manager's snapshot -
    // which is the file as it was last saved.
    void testTheFileLookupPrefersTheOpenDocument()
    {
        Utils::TemporaryDirectory dir("cpp-refactoring-file-lookup");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("int main()\n{\n    return 0;\n}\n"));

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(factory, "no editor factory claims a C++ file");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);

        // Unsaved, so that the snapshot's parse and the document's differ and
        // the lookup has to choose.
        QTextCursor typing(document->document());
        typing.movePosition(QTextCursor::End);
        typing.insertText("int later() { return 1; }\n");
        document->recalculateSemanticInfo();
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the unsaved file says");

        const CppRefactoringFilePtr found
            = CppRefactoringChanges(CppModelManager::snapshot()).cppFile(file);
        QVERIFY2(found->cppDocument(), "the lookup found no parse of the file at all");
        QCOMPARE(found->cppDocument().data(), document->semanticInfo().doc.data());
    }
};

QObject *createQuickFixAssistTest()
{
    return new QuickFixAssistTest;
}

#endif // WITH_TESTS

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include "cppquickfixassistant.moc"
#endif

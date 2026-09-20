// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppfunctionparamrenaminghandler.h"

#include "cpptoolsreuse.h"

#include "cppeditordocument.h"
#include "cppfunctiondecldeflink.h"
#include "cpplocalrenaming.h"
#include "cppsemanticinfo.h"

#include <coreplugin/editormanager/ieditor.h>

#include <cplusplus/AST.h>
#include <cplusplus/ASTPath.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <memory>

using namespace CPlusPlus;

namespace CppEditor::Internal {

using DeclDefLinkPtr = std::shared_ptr<FunctionDeclDefLink>;

class CppFunctionParamRenamingHandler::Private
{
public:
    Private(CppLocalRenaming &localRenaming);

    void handleRenamingStarted();
    void handleRenamingFinished();
    void handleLinkFound(const DeclDefLinkPtr &link);
    void findLink(FunctionDefinitionAST &func, const SemanticInfo &semanticInfo);

    CppEditorDocument *document() const
    {
        return qobject_cast<CppEditorDocument *>(localRenaming.textDocument());
    }

    CppLocalRenaming &localRenaming;
    std::unique_ptr<FunctionDeclDefLinkFinder> linkFinder;
    DeclDefLinkPtr link;
};

CppFunctionParamRenamingHandler::CppFunctionParamRenamingHandler(
    CppLocalRenaming &localRenaming, QObject *parent)
    : QObject(parent), d(new Private(localRenaming)) {}

CppFunctionParamRenamingHandler::~CppFunctionParamRenamingHandler() { delete d; }

#ifdef WITH_TESTS
bool CppFunctionParamRenamingHandler::waitingForDeclaration() const
{
    return d->link != nullptr;
}
#endif

CppFunctionParamRenamingHandler::Private::Private(CppLocalRenaming &localRenaming)
    : localRenaming(localRenaming)
{
    QObject::connect(&localRenaming, &CppLocalRenaming::started,
                     &localRenaming, [this] { handleRenamingStarted(); });
    QObject::connect(&localRenaming, &CppLocalRenaming::finished,
                     &localRenaming, [this] { handleRenamingFinished(); });
}

void CppFunctionParamRenamingHandler::Private::handleRenamingStarted()
{
    linkFinder.reset();
    link.reset();

    Core::IEditor * const editor = localRenaming.editor();
    const CppEditorDocument * const cppDocument = document();
    if (!editor || !cppDocument)
        return;

    // Are we currently on the function signature? In this case, the normal decl/def link
    // mechanism kicks in and we don't have to do anything.
    const CppDeclDefLinkController * const controller = declDefLinkControllerFor(editor);
    if (controller && controller->link())
        return;

    // If we find a surrounding function definition, start up the decl/def link finder.
    const SemanticInfo semanticInfo = cppDocument->semanticInfo();
    if (!semanticInfo.doc || !semanticInfo.doc->translationUnit())
        return;
    const QList<AST *> astPath = ASTPath(semanticInfo.doc)(TextEditor::textCursorOf(editor));
    for (auto it = astPath.rbegin(); it != astPath.rend(); ++it) {
        if (const auto func = (*it)->asFunctionDefinition()) {
            findLink(*func, semanticInfo);
            return;
        }
    }
}

void CppFunctionParamRenamingHandler::Private::handleRenamingFinished()
{
    if (link) {
        link->apply(localRenaming.editor(), false);
        link.reset();
    }
}

void CppFunctionParamRenamingHandler::Private::handleLinkFound(const DeclDefLinkPtr &link)
{
    if (localRenaming.isActive())
        this->link = link;
    linkFinder.release()->deleteLater();
}

void CppFunctionParamRenamingHandler::Private::findLink(FunctionDefinitionAST &func,
                                                        const SemanticInfo &semanticInfo)
{
    if (!func.declarator)
        return;

    const CppEditorDocument * const cppDocument = document();
    if (!cppDocument)
        return;

    // The finder needs a cursor that points to the signature, so provide one.
    QTextDocument * const doc = cppDocument->document();
    const int pos = semanticInfo.doc->translationUnit()->getTokenEndPositionInDocument(
        func.declarator->firstToken(), doc);
    QTextCursor cursor(doc);
    cursor.setPosition(pos);
    linkFinder.reset(new FunctionDeclDefLinkFinder);
    QObject::connect(linkFinder.get(), &FunctionDeclDefLinkFinder::foundLink,
            &localRenaming, [this](const DeclDefLinkPtr &link) {
        handleLinkFound(link);
    });
    linkFinder->startFindLinkAt(cursor, semanticInfo.doc, semanticInfo.snapshot);
}

} // namespace CppEditor::Internal

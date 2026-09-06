// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmljseditordocument.h"

#include "qmljseditorsettings.h"

#include <utils/mimeconstants.h>

#include "qmljscompletionassist.h"
#include "qmljsfindreferences.h"
#include <texteditor/symbolrequests.h>

#include <texteditor/texteditor.h>

#include <texteditor/fontsettings.h>

#ifdef WITH_TESTS
#include "qmljseditorconstants.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/editormanager/editormanager.h>

#include <utils/mimeconstants.h>
#include <utils/temporarydirectory.h>

#include <QMenu>
#include <QScopeGuard>

#include <QTest>
#endif

#include "qmljseditordocument_p.h"
#include "qmljseditorplugin.h"
#include "qmljseditortr.h"
#include "qmljshighlighter.h"
#include "qmljsquickfix.h"
#include "qmljsquickfixassist.h" // required to resolve type of Internal::quickFixAssistProvider()
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/genericproposalmodel.h>
#include <texteditor/codeassist/iassistprocessor.h>
#include <texteditor/codeassist/iassistproposal.h>
#include <texteditor/quickfix.h>
#include "qmljssemantichighlighter.h"
#include "qmljssemanticinfoupdater.h"
#include "qmljstextmark.h"
#include "qmllsclientsettings.h"
#include "qmloutlinemodel.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/modemanager.h>

#include <projectexplorer/projectmanager.h>

#include <qmljs/parser/qmljsast_p.h>
#include <qmljs/qmljsmodelmanagerinterface.h>
#include <qmljstools/qmljsindenter.h>
#include <qmljstools/qmljsqtstylecodeformatter.h>

#include <texteditor/refactoringchanges.h>

#include <utils/infobar.h>

#include <QDebug>
#include <QLoggingCategory>

const char QML_UI_FILE_WARNING[] = "QmlJSEditor.QmlUiFileWarning";

using namespace QmlJSEditor;
using namespace QmlJS;
using namespace QmlJS::AST;
using namespace QmlJSTools;
using namespace Utils;

namespace {

enum {
    UPDATE_DOCUMENT_DEFAULT_INTERVAL = 100,
    UPDATE_OUTLINE_INTERVAL = 500
};

struct Declaration
{
    QString text;
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;
};

class FindIdDeclarations: protected Visitor
{
public:
    using Result = QHash<QString, QList<SourceLocation> >;

    Result operator()(Document::Ptr doc)
    {
        _ids.clear();
        _maybeIds.clear();
        if (doc && doc->qmlProgram())
            doc->qmlProgram()->accept(this);
        return _ids;
    }

protected:
    QString asString(AST::UiQualifiedId *id)
    {
        QString text;
        for (; id; id = id->next) {
            if (!id->name.isEmpty())
                text += id->name.toString();
            else
                text += QLatin1Char('?');

            if (id->next)
                text += QLatin1Char('.');
        }

        return text;
    }

    void accept(AST::Node *node) { AST::Node::accept(node, this); }

    using Visitor::visit;
    using Visitor::endVisit;

    bool visit(AST::UiScriptBinding *node) override
    {
        if (asString(node->qualifiedId) == QLatin1String("id")) {
            if (auto stmt = AST::cast<const AST::ExpressionStatement*>(node->statement)) {
                if (auto idExpr = AST::cast<const AST::IdentifierExpression *>(stmt->expression)) {
                    if (!idExpr->name.isEmpty()) {
                        const QString &id = idExpr->name.toString();
                        QList<SourceLocation> *locs = &_ids[id];
                        locs->append(idExpr->firstSourceLocation());
                        locs->append(_maybeIds.value(id));
                        _maybeIds.remove(id);
                        return false;
                    }
                }
            }
        }

        accept(node->statement);

        return false;
    }

    bool visit(AST::IdentifierExpression *node) override
    {
        if (!node->name.isEmpty()) {
            const QString &name = node->name.toString();

            if (_ids.contains(name))
                _ids[name].append(node->identifierToken);
            else
                _maybeIds[name].append(node->identifierToken);
        }
        return false;
    }

    void throwRecursionDepthError() override
    {
        qWarning("Warning: Hit maximum recursion depth while visiting AST in FindIdDeclarations");
    }

private:
    Result _ids;
    Result _maybeIds;
};

class FindDeclarations: protected Visitor
{
    QList<Declaration> _declarations;
    int _depth = 0;

public:
    QList<Declaration> operator()(AST::Node *node)
    {
        _depth = -1;
        _declarations.clear();
        accept(node);
        return _declarations;
    }

protected:
    using Visitor::visit;
    using Visitor::endVisit;

    QString asString(AST::UiQualifiedId *id)
    {
        QString text;
        for (; id; id = id->next) {
            if (!id->name.isEmpty())
                text += id->name.toString();
            else
                text += QLatin1Char('?');

            if (id->next)
                text += QLatin1Char('.');
        }

        return text;
    }

    void accept(AST::Node *node) { AST::Node::accept(node, this); }

    void init(Declaration *decl, AST::UiObjectMember *member)
    {
        const SourceLocation first = member->firstSourceLocation();
        const SourceLocation last = member->lastSourceLocation();
        decl->startLine = first.startLine;
        decl->startColumn = first.startColumn;
        decl->endLine = last.startLine;
        decl->endColumn = last.startColumn + last.length;
    }

    void init(Declaration *decl, AST::ExpressionNode *expressionNode)
    {
        const SourceLocation first = expressionNode->firstSourceLocation();
        const SourceLocation last = expressionNode->lastSourceLocation();
        decl->startLine = first.startLine;
        decl->startColumn = first.startColumn;
        decl->endLine = last.startLine;
        decl->endColumn = last.startColumn + last.length;
    }

    bool visit(AST::UiObjectDefinition *node) override
    {
        ++_depth;

        Declaration decl;
        init(&decl, node);

        decl.text.fill(QLatin1Char(' '), _depth);
        if (node->qualifiedTypeNameId)
            decl.text.append(asString(node->qualifiedTypeNameId));
        else
            decl.text.append(QLatin1Char('?'));

        _declarations.append(decl);

        return true; // search for more bindings
    }

    void endVisit(AST::UiObjectDefinition *) override
    {
        --_depth;
    }

    bool visit(AST::UiObjectBinding *node) override
    {
        ++_depth;

        Declaration decl;
        init(&decl, node);

        decl.text.fill(QLatin1Char(' '), _depth);

        decl.text.append(asString(node->qualifiedId));
        decl.text.append(QLatin1String(": "));

        if (node->qualifiedTypeNameId)
            decl.text.append(asString(node->qualifiedTypeNameId));
        else
            decl.text.append(QLatin1Char('?'));

        _declarations.append(decl);

        return true; // search for more bindings
    }

    void endVisit(AST::UiObjectBinding *) override
    {
        --_depth;
    }

    bool visit(AST::UiScriptBinding *) override
    {
        ++_depth;

#if 0 // ### ignore script bindings for now.
        Declaration decl;
        init(&decl, node);

        decl.text.fill(QLatin1Char(' '), _depth);
        decl.text.append(asString(node->qualifiedId));

        _declarations.append(decl);
#endif

        return false; // more more bindings in this subtree.
    }

    void endVisit(AST::UiScriptBinding *) override
    {
        --_depth;
    }

    bool visit(AST::TemplateLiteral *ast) override
    {
        // avoid? finds function declarations in templates
        AST::Node::accept(ast->expression, this);
        return true;
    }

    bool visit(AST::FunctionExpression *) override
    {
        return false;
    }

    bool visit(AST::FunctionDeclaration *ast) override
    {
        if (ast->name.isEmpty())
            return false;

        Declaration decl;
        init(&decl, ast);

        decl.text.fill(QLatin1Char(' '), _depth);
        decl.text += ast->name.toString();

        decl.text += QLatin1Char('(');
        for (FormalParameterList *it = ast->formals; it; it = it->next) {
            if (!it->element->bindingIdentifier.isEmpty())
                decl.text += it->element->bindingIdentifier.toString();

            if (it->next)
                decl.text += QLatin1String(", ");
        }

        decl.text += QLatin1Char(')');

        _declarations.append(decl);

        return false;
    }

    bool visit(AST::PatternElement *ast) override
    {
        if (!ast->isVariableDeclaration() || ast->bindingIdentifier.isEmpty())
            return false;

        Declaration decl;
        decl.text.fill(QLatin1Char(' '), _depth);
        decl.text += ast->bindingIdentifier.toString();

        const SourceLocation first = ast->identifierToken;
        decl.startLine = first.startLine;
        decl.startColumn = first.startColumn;
        decl.endLine = first.startLine;
        decl.endColumn = first.startColumn + first.length;

        _declarations.append(decl);

        return false;
    }

    bool visit(AST::BinaryExpression *ast) override
    {
        auto field = AST::cast<const AST::FieldMemberExpression *>(ast->left);
        auto funcExpr = AST::cast<const AST::FunctionExpression *>(ast->right);

        if (field && funcExpr && funcExpr->body && (ast->op == QSOperator::Assign)) {
            Declaration decl;
            init(&decl, ast);

            decl.text.fill(QLatin1Char(' '), _depth);
            decl.text += field->name.toString();

            decl.text += QLatin1Char('(');
            for (FormalParameterList *it = funcExpr->formals; it; it = it->next) {
                if (!it->element->bindingIdentifier.isEmpty())
                    decl.text += it->element->bindingIdentifier.toString();

                if (it->next)
                    decl.text += QLatin1String(", ");
            }
            decl.text += QLatin1Char(')');

            _declarations.append(decl);
        }

        return true;
    }
};

class CreateRanges: protected AST::Visitor
{
    QTextDocument *_textDocument = nullptr;
    QList<Range> _ranges;

public:
    QList<Range> operator()(QTextDocument *textDocument, Document::Ptr doc)
    {
        _textDocument = textDocument;
        _ranges.clear();
        if (doc && doc->ast() != nullptr)
            doc->ast()->accept(this);
        return _ranges;
    }

protected:
    using AST::Visitor::visit;

    bool visit(AST::UiObjectBinding *ast) override
    {
        if (ast->initializer && ast->initializer->lbraceToken.length)
            _ranges.append(createRange(ast, ast->initializer));
        return true;
    }

    bool visit(AST::UiObjectDefinition *ast) override
    {
        if (ast->initializer && ast->initializer->lbraceToken.length)
            _ranges.append(createRange(ast, ast->initializer));
        return true;
    }

    bool visit(AST::FunctionExpression *ast) override
    {
        _ranges.append(createRange(ast));
        return true;
    }

    bool visit(AST::TemplateLiteral *ast) override
    {
        AST::Node::accept(ast->expression, this);
        return true;
    }

    bool visit(AST::FunctionDeclaration *ast) override
    {
        _ranges.append(createRange(ast));
        return true;
    }

    bool visit(AST::BinaryExpression *ast) override
    {
        auto field = AST::cast<AST::FieldMemberExpression *>(ast->left);
        auto funcExpr = AST::cast<AST::FunctionExpression *>(ast->right);

        if (field && funcExpr && funcExpr->body && (ast->op == QSOperator::Assign))
            _ranges.append(createRange(ast, ast->firstSourceLocation(), ast->lastSourceLocation()));
        return true;
    }

    bool visit(AST::UiScriptBinding *ast) override
    {
        if (auto block = AST::cast<AST::Block *>(ast->statement))
            _ranges.append(createRange(ast, block));
        return true;
    }

    void throwRecursionDepthError() override
    {
        qWarning("Warning: Hit maximum recursion depth while visiting AST in CreateRanges");
    }

    Range createRange(AST::UiObjectMember *member, AST::UiObjectInitializer *ast)
    {
        return createRange(member, member->firstSourceLocation(), ast->rbraceToken);
    }

    Range createRange(AST::FunctionExpression *ast)
    {
        return createRange(ast, ast->lbraceToken, ast->rbraceToken);
    }

    Range createRange(AST::UiScriptBinding *ast, AST::Block *block)
    {
        return createRange(ast, block->lbraceToken, block->rbraceToken);
    }

    Range createRange(AST::Node *ast, SourceLocation start, SourceLocation end)
    {
        Range range;

        range.ast = ast;

        range.begin = QTextCursor(_textDocument);
        range.begin.setPosition(start.begin());

        range.end = QTextCursor(_textDocument);
        range.end.setPosition(end.end());

        return range;
    }
};

}

namespace QmlJSEditor {
namespace Internal {

QmlJSEditorDocumentPrivate::QmlJSEditorDocumentPrivate(QmlJSEditorDocument *parent)
    : q(parent)
    , m_semanticHighlighter(new SemanticHighlighter(parent))
    , m_outlineModel(new QmlOutlineModel(parent))
{
    ModelManagerInterface *modelManager = ModelManagerInterface::instance();

    // code model
    m_updateDocumentTimer.setInterval(UPDATE_DOCUMENT_DEFAULT_INTERVAL);
    m_updateDocumentTimer.setSingleShot(true);
    connect(q->document(), &QTextDocument::contentsChanged,
            &m_updateDocumentTimer, QOverload<>::of(&QTimer::start));
    connect(&m_updateDocumentTimer, &QTimer::timeout,
            this, &QmlJSEditorDocumentPrivate::reparseDocument);
    connect(modelManager, &ModelManagerInterface::documentUpdated,
            this, &QmlJSEditorDocumentPrivate::onDocumentUpdated);

    // semantic info
    m_semanticInfoUpdater = new SemanticInfoUpdater();
    connect(m_semanticInfoUpdater, &SemanticInfoUpdater::finished,
            m_semanticInfoUpdater, &QObject::deleteLater);
    connect(m_semanticInfoUpdater, &SemanticInfoUpdater::updated,
            this, &QmlJSEditorDocumentPrivate::acceptNewSemanticInfo);
    m_semanticInfoUpdater->start();

    // library info changes
    m_reupdateSemanticInfoTimer.setInterval(UPDATE_DOCUMENT_DEFAULT_INTERVAL);
    m_reupdateSemanticInfoTimer.setSingleShot(true);
    connect(&m_reupdateSemanticInfoTimer, &QTimer::timeout,
            this, &QmlJSEditorDocumentPrivate::reupdateSemanticInfo);
    connect(modelManager, &ModelManagerInterface::libraryInfoUpdated,
            &m_reupdateSemanticInfoTimer, QOverload<>::of(&QTimer::start));

    // outline model
    m_updateOutlineModelTimer.setInterval(UPDATE_OUTLINE_INTERVAL);
    m_updateOutlineModelTimer.setSingleShot(true);
    connect(&m_updateOutlineModelTimer, &QTimer::timeout,
            this, &QmlJSEditorDocumentPrivate::updateOutlineModel);

    modelManager->updateSourceFiles({parent->filePath()}, false);
}

QmlJSEditorDocumentPrivate::~QmlJSEditorDocumentPrivate()
{
    m_semanticInfoUpdater->abort();
    // clean up all marks, otherwise a callback could try to access deleted members.
    // see QTCREATORBUG-20199
    cleanDiagnosticMarks();
    cleanSemanticMarks();
}

void QmlJSEditorDocumentPrivate::invalidateFormatterCache()
{
    CreatorCodeFormatter formatter(q->tabSettings());
    formatter.invalidateCache(q->document());
}

void QmlJSEditorDocumentPrivate::reparseDocument()
{
    ModelManagerInterface::instance()->updateSourceFiles({q->filePath()}, false);
}

void QmlJSEditorDocumentPrivate::onDocumentUpdated(Document::Ptr doc)
{
    if (q->filePath() != doc->fileName())
        return;

    // text document has changed, simply wait for the next onDocumentUpdated
    if (doc->editorRevision() != q->document()->revision())
        return;

    cleanDiagnosticMarks();
    if (doc->ast()) {
        // got a correctly parsed (or recovered) file.
        m_semanticInfoDocRevision = doc->editorRevision();
        m_semanticInfoUpdater->update(doc, ModelManagerInterface::instance()->snapshot());
    } else if (doc->language().isFullySupportedLanguage()
               && m_qmllsStatus.semanticWarningsSource == QmllsStatus::Source::EmbeddedCodeModel) {
        createTextMarks(doc->diagnosticMessages());
    }
    q->updateCodeWarnings(doc);
}

void QmlJSEditorDocumentPrivate::reupdateSemanticInfo()
{
    // If the editor is newer than the semantic info (possibly with update in progress),
    // new semantic infos won't be accepted anyway. We'll get a onDocumentUpdated anyhow.
    if (q->document()->revision() != m_semanticInfoDocRevision)
        return;

    m_semanticInfoUpdater->reupdate(ModelManagerInterface::instance()->snapshot());
}

void QmlJSEditorDocumentPrivate::acceptNewSemanticInfo(const SemanticInfo &semanticInfo)
{
    if (semanticInfo.revision() != q->document()->revision()) {
        // ignore outdated semantic infos
        return;
    }

    m_semanticInfo = semanticInfo;
    Document::Ptr doc = semanticInfo.document;

    // create the ranges
    CreateRanges createRanges;
    m_semanticInfo.ranges = createRanges(q->document(), doc);

    // Refresh the ids
    FindIdDeclarations updateIds;
    m_semanticInfo.idLocations = updateIds(doc);

    m_outlineModelNeedsUpdate = true;
    m_semanticHighlightingNecessary = true;

    if (m_qmllsStatus.semanticWarningsSource == QmllsStatus::Source::EmbeddedCodeModel)
        createTextMarks(m_semanticInfo);
    emit q->semanticInfoUpdated(m_semanticInfo); // calls triggerPendingUpdates as necessary
}

void QmlJSEditorDocumentPrivate::updateOutlineModel()
{
    if (isSemanticInfoOutdated())
        return; // outline update will be retriggered when semantic info is updated

    m_outlineModel->update(m_semanticInfo);
}

bool QmlJSEditorDocumentPrivate::isSemanticInfoOutdated() const
{
    return m_semanticInfo.revision() != q->document()->revision();
}

static void cleanMarks(QVector<TextEditor::TextMark *> *marks, TextEditor::TextDocument *doc)
{
    // if doc is null, this method is improperly called, so better do nothing that leave an
    // inconsistent state where marks are cleared but not removed from doc.
    if (!marks || !doc)
        return;
    for (TextEditor::TextMark *mark : std::as_const(*marks)) {
        doc->removeMark(mark);
        delete mark;
    }
    marks->clear();
}

void QmlJSEditorDocumentPrivate::createTextMarks(const QList<DiagnosticMessage> &diagnostics)
{
    if (m_qmllsStatus.semanticWarningsSource != QmllsStatus::Source::EmbeddedCodeModel)
        return;
    for (const DiagnosticMessage &diagnostic : diagnostics) {
        const auto onMarkRemoved = [this](QmlJSTextMark *mark) {
            m_diagnosticMarks.removeAll(mark);
            delete mark;
         };

        auto mark = new QmlJSTextMark(q->filePath(), diagnostic, onMarkRemoved);
        m_diagnosticMarks.append(mark);
        q->addMark(mark);
    }
}

void QmlJSEditorDocumentPrivate::cleanDiagnosticMarks()
{
    cleanMarks(&m_diagnosticMarks, q);
}

void QmlJSEditorDocumentPrivate::createTextMarks(const SemanticInfo &info)
{
    cleanSemanticMarks();
    const auto onMarkRemoved = [this](QmlJSTextMark *mark) {
        m_semanticMarks.removeAll(mark);
        delete mark;
    };
    for (const DiagnosticMessage &diagnostic : std::as_const(info.semanticMessages)) {
        auto mark = new QmlJSTextMark(q->filePath(),
                                      diagnostic, onMarkRemoved);
        m_semanticMarks.append(mark);
        q->addMark(mark);
    }
    for (const QmlJS::StaticAnalysis::Message &message : std::as_const(info.staticAnalysisMessages)) {
        auto mark = new QmlJSTextMark(q->filePath(),
                                      message, onMarkRemoved);
        m_semanticMarks.append(mark);
        q->addMark(mark);
    }
}

void QmlJSEditorDocumentPrivate::cleanSemanticMarks()
{
    cleanMarks(&m_semanticMarks, q);
}

void QmlJSEditorDocumentPrivate::setSemanticWarningSource(QmllsStatus::Source newSource)
{
    if (m_qmllsStatus.semanticWarningsSource == newSource)
        return;
    m_qmllsStatus.semanticWarningsSource = newSource;
    QTC_ASSERT(q->thread() == QThread::currentThread(), return );
    switch (m_qmllsStatus.semanticWarningsSource) {
    case QmllsStatus::Source::Qmlls:
        m_semanticHighlighter->setEnableWarnings(false);
        cleanDiagnosticMarks();
        cleanSemanticMarks();
        if (m_semanticInfo.isValid() && !isSemanticInfoOutdated()) {
            // clean up underlines for warning messages
            m_semanticHighlightingNecessary = false;
            m_semanticHighlighter->rerun(m_semanticInfo);
        }
        break;
    case QmllsStatus::Source::EmbeddedCodeModel:
        m_semanticHighlighter->setEnableWarnings(true);
        reparseDocument();
        break;
    }
}

void QmlJSEditorDocumentPrivate::setSemanticHighlightSource(QmllsStatus::Source newSource)
{
    if (m_qmllsStatus.semanticHighlightSource == newSource)
        return;
    m_qmllsStatus.semanticHighlightSource = newSource;
    QTC_ASSERT(q->thread() == QThread::currentThread(), return );
    switch (m_qmllsStatus.semanticHighlightSource) {
    case QmllsStatus::Source::Qmlls:
        m_semanticHighlighter->setEnableHighlighting(false);
        cleanSemanticMarks();
        break;
    case QmllsStatus::Source::EmbeddedCodeModel:
        m_semanticHighlighter->setEnableHighlighting(true);
        if (m_semanticInfo.isValid() && !isSemanticInfoOutdated()) {
            m_semanticHighlightingNecessary = false;
            m_semanticHighlighter->rerun(m_semanticInfo);
        }
        break;
    }
}

void QmlJSEditorDocumentPrivate::setCompletionSource(QmllsStatus::Source newSource)
{
    if (m_qmllsStatus.completionSource == newSource)
        return;
    m_qmllsStatus.completionSource = newSource;
    switch (m_qmllsStatus.completionSource) {
    case QmllsStatus::Source::Qmlls:
        // activation of the document already takes care of setting it
        break;
    case QmllsStatus::Source::EmbeddedCodeModel:
        // deactivation of the document takes care of restoring it
        break;
    }
}

void QmlJSEditorDocumentPrivate::setSourcesWithCapabilities(
    const LanguageServerProtocol::ServerCapabilities &cap)
{
    if (cap.completionProvider())
        setCompletionSource(QmllsStatus::Source::Qmlls);
    else
        setCompletionSource(QmllsStatus::Source::EmbeddedCodeModel);
    if (cap.codeActionProvider())
        setSemanticWarningSource(QmllsStatus::Source::Qmlls);
    else
        setSemanticWarningSource(QmllsStatus::Source::EmbeddedCodeModel);
    if (cap.semanticTokensProvider() && qmllsSettings()->useQmllsSemanticHighlighting())
        setSemanticHighlightSource(QmllsStatus::Source::Qmlls);
    else
        setSemanticHighlightSource(QmllsStatus::Source::EmbeddedCodeModel);
}

} // Internal

QmlJSEditorDocument::QmlJSEditorDocument(Utils::Id id)
    : d(new Internal::QmlJSEditorDocumentPrivate(this))
{
    setId(id);
    connect(this, &TextEditor::TextDocument::tabSettingsChanged,
            d, &Internal::QmlJSEditorDocumentPrivate::invalidateFormatterCache);
    resetSyntaxHighlighter([] { return new QmlJSHighlighter(); });
    setSupportedEncodings({TextEncoding::Utf8}); // qml files are defined to be utf-8
    setIndenter(createQmlJsIndenter(document()));
}


class RefactoringFileWithoutReindenting : public TextEditor::RefactoringFile
{
public:
    static void applyChangeSet(const Utils::FilePath &filePath, const ChangeSet &changeSet);

protected:
    using TextEditor::RefactoringFile::RefactoringFile;
    void doFormatting() override {}
};

void RefactoringFileWithoutReindenting::applyChangeSet(
    const Utils::FilePath &filePath, const ChangeSet &changeSet)
{
    RefactoringFileWithoutReindenting(filePath).apply(changeSet);
}

void QmlJSEditorDocument::autoFormat(const QTextCursor &cursor)
{
    if (!formatter())
        return;
    formatter()->format(cursor, tabSettings(), [this](const ChangeSet &result) {
        RefactoringFileWithoutReindenting::applyChangeSet(filePath(), result);
    });
}

QmlJSEditorDocument::~QmlJSEditorDocument()
{
    delete d;
}

const SemanticInfo &QmlJSEditorDocument::semanticInfo() const
{
    return d->m_semanticInfo;
}

bool QmlJSEditorDocument::isSemanticInfoOutdated() const
{
    return d->isSemanticInfoOutdated();
}

QVector<QTextLayout::FormatRange> QmlJSEditorDocument::diagnosticRanges() const
{
    return d->m_diagnosticRanges;
}

Internal::QmlOutlineModel *QmlJSEditorDocument::outlineModel() const
{
    return d->m_outlineModel;
}

static void appendExtraSelectionsForMessages(
        QList<TextEditor::TextDocument::ExtraSelection> *selections,
        const QList<DiagnosticMessage> &messages,
        const QTextDocument *document)
{
    for (const DiagnosticMessage &d : messages) {
        const int line = d.loc.startLine;
        const int column = qMax(1U, d.loc.startColumn);

        TextEditor::TextDocument::ExtraSelection sel;
        QTextCursor c(document->findBlockByNumber(line - 1));
        sel.cursor = c;

        sel.cursor.setPosition(c.position() + column - 1);

        if (d.loc.length == 0) {
            if (sel.cursor.atBlockEnd())
                sel.cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);
            else
                sel.cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        } else {
            sel.cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, d.loc.length);
        }

        const auto fontSettings = TextEditor::globalFontSettings().data();

        if (d.isWarning())
            sel.format = fontSettings.toTextCharFormat(TextEditor::C_WARNING);
        else
            sel.format = fontSettings.toTextCharFormat(TextEditor::C_ERROR);

        sel.format.setToolTip(d.message);

        selections->append(sel);
    }
}

// The parse errors, as extra selections on the document. Where they were on
// the editor widget, a QML file open in anything else underlined nothing -
// and a widget cannot be asked for them by whoever else is showing the file.
void QmlJSEditorDocument::updateCodeWarnings(QmlJS::Document::Ptr doc)
{
    QList<TextEditor::TextDocument::ExtraSelection> selections;
    if (!doc->ast() && doc->language().isFullySupportedLanguage())
        appendExtraSelectionsForMessages(&selections, doc->diagnosticMessages(), document());
    setExtraSelections(TextEditor::TextEditorWidget::CodeWarningsSelection, selections);
}

void QmlJSEditorDocument::foldOnFirstOpen()
{
    TextDocument::foldOnFirstOpen();

    using namespace Utils::Constants;
    static const QStringList qmlTypes{QML_MIMETYPE, QBS_MIMETYPE, QMLTYPES_MIMETYPE, QMLUI_MIMETYPE};
    if (Internal::settings().foldAuxData() && qmlTypes.contains(mimeType()))
        foldAuxiliaryData();
}

void QmlJSEditorDocument::foldAuxiliaryData()
{
    QTextDocument * const doc = document();
    auto * const documentLayout = qobject_cast<TextEditor::TextDocumentLayout *>(
        doc->documentLayout());
    QTC_ASSERT(documentLayout, return);

    QTextBlock block = doc->lastBlock();
    while (block.isValid() && block.isVisible()) {
        if (TextEditor::TextBlockUserData::canFold(block) && block.next().isVisible()) {
            if (block.text().trimmed().startsWith("/*##^##")) {
                TextEditor::TextBlockUserData::doFoldOrUnfold(block, false);
                documentLayout->requestUpdate();
                documentLayout->emitDocumentSizeChanged();
                break;
            }
        }
        block = block.previous();
    }
}

QList<QAction *> QmlJSEditorDocument::contextMenuActions(const QTextCursor &cursor)
{
    // Nowhere in particular is not a place; and a parse that is out of date -
    // or one that has not produced a document yet - proposes nothing. The
    // second half is easy to miss: isSemanticInfoOutdated() goes false before
    // semanticInfo().document is filled in, and the quick fixes need that
    // document.
    if (cursor.isNull() || isSemanticInfoOutdated() || semanticInfo().document.isNull())
        return {};

    if (!m_refactoringAction) {
        m_refactoringAction = new QAction(Tr::tr("Refactoring"), this);
        auto * const menu = new QMenu;
        m_refactoringAction->setMenu(menu);
        // A QAction does not own the menu it carries.
        connect(this, &QObject::destroyed, menu, [menu] { delete menu; });
    }
    QMenu * const menu = m_refactoringAction->menu();
    menu->clear();
    m_refactoringAction->setEnabled(false);

    std::unique_ptr<TextEditor::AssistInterface> interface
        = createAssistInterface(cursor, TextEditor::QuickFix,
                                TextEditor::ExplicitlyInvoked, nullptr);
    if (!interface)
        return {m_refactoringAction};

    TextEditor::IAssistProcessor * const processor
        = quickFixAssistProvider()->createProcessor(interface.get());
    QAction * const action = m_refactoringAction;
    const auto fill = [menu = QPointer(menu), action = QPointer(action), processor](
                          TextEditor::IAssistProposal *proposal) {
        const QScopedPointer<TextEditor::IAssistProposal> holdProposal(proposal);
        const QScopedPointer<TextEditor::IAssistProcessor> holdProcessor(processor);
        if (!menu || !proposal)
            return;
        const auto model = proposal->model().staticCast<TextEditor::GenericProposalModel>();
        for (int index = 0; index < model->size(); ++index) {
            auto * const item
                = static_cast<const TextEditor::AssistProposalItem *>(model->proposalItem(index));
            const TextEditor::QuickFixOperation::Ptr operation
                = item->data().value<TextEditor::QuickFixOperation::Ptr>();
            QAction * const entry = menu->addAction(operation->description());
            connect(entry, &QAction::triggered, menu, [operation] { operation->perform(); });
        }
        if (action)
            action->setEnabled(!menu->isEmpty());
    };

    // Answered on the spot where the language can, and later where it cannot -
    // the menu is already open by then, which is what a QMenu copes with.
    if (TextEditor::IAssistProposal * const proposal = processor->start(std::move(interface)))
        fill(proposal);
    else
        processor->setAsyncCompletionAvailableHandler(fill);

    return {m_refactoringAction};
}

std::unique_ptr<TextEditor::AssistInterface> QmlJSEditorDocument::createAssistInterface(
    const QTextCursor &cursor, TextEditor::AssistKind kind, TextEditor::AssistReason reason,
    Core::IEditor *editor) const
{
    if (kind == TextEditor::Completion) {
        return std::make_unique<QmlJSCompletionAssistInterface>(cursor, filePath(), reason,
                                                                semanticInfo());
    }
    if (kind == TextEditor::QuickFix) {
        return std::make_unique<Internal::QmlJSQuickFixAssistInterface>(
            const_cast<QmlJSEditorDocument *>(this), cursor, semanticInfo(), reason);
    }
    return TextDocument::createAssistInterface(cursor, kind, reason, editor);
}

#ifdef WITH_TESTS

// Declared above anything with a raw string in it: moc's namespace tracking
// does not survive those, and a Q_OBJECT class after them comes out
// unqualified.
class QmlJSEditorDocumentTest final : public QObject
{
    Q_OBJECT

private slots:
    // The block the QML designer writes at the end of a file is folded when
    // the file is opened fresh. That was the editor widget's restoreState()
    // override, so a QML file open in anything else showed it unfolded.
    void testTheDesignerBlockIsFoldedOnFirstOpen()
    {
        Utils::TemporaryDirectory dir("qmljs-fold-aux");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("Fold.qml");
        QVERIFY(file.writeFileContents(
            "import QtQuick\nItem {\n    width: 10\n}\n"
            "/*##^##\nDesigner {\n    D{i:0}\n}\n##^##*/\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<QmlJSEditorDocument *>(editor->document());
        QVERIFY(document);

        const auto blockStartingTheAuxData = [document] {
            for (QTextBlock block = document->document()->begin(); block.isValid();
                 block = block.next()) {
                if (block.text().trimmed().startsWith("/*##^##"))
                    return block;
            }
            return QTextBlock();
        };
        const QTextBlock marker = blockStartingTheAuxData();
        QVERIFY2(marker.isValid(), "the test file has no designer block in it");

        // Folded means the block after it is hidden; the marker line itself
        // stays visible, which is what a fold looks like.
        QTRY_VERIFY2(!marker.next().isVisible(),
                     "the designer block was left unfolded");
        QVERIFY2(marker.isVisible(), "the line that opens the block was hidden too");

        // Asked of the document, with no widget anywhere in the assertion -
        // which is the point: this used to be a TextEditorWidget override.
        QVERIFY2(TextEditor::TextBlockUserData::isFolded(marker),
                 "the block is hidden but the document does not call it folded");
    }

    // A right click in a QML file offers Refactoring, whose entries are the
    // quick fixes proposed at that place. They cannot live in an ActionManager
    // container, so the document offers them.
    void testTheRefactoringMenuComesFromTheDocument()
    {
        Utils::TemporaryDirectory dir("qmljs-refactoring-menu");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("Test.qml");
        // The shape QmlJSQuickFixTest uses for Split Initializer, so there is
        // known to be something to offer.
        QVERIFY(file.writeFileContents(
            "import QtQuick\nItem {\n    Item { x: 10; y: 20; width: 10 }\n}\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<QmlJSEditorDocument *>(editor->document());
        QVERIFY(document);

        // Both halves: isSemanticInfoOutdated() goes false before the document
        // is filled in, and the quick fixes need that document.
        QTRY_VERIFY2(!document->isSemanticInfoOutdated()
                         && !document->semanticInfo().document.isNull(),
                     "the parse never produced a document");

        QTextCursor cursor(document->document());
        // On the inner "Item", where Split Initializer applies.
        cursor.setPosition(document->plainText().indexOf("Item { x:") + 2);
        // Where the reader is, not just what is asked about: a quick fix reads
        // the position from its refactoring file, which is the caret of
        // whichever view shows the document.
        TextEditor::setTextCursorOf(editor, cursor);

        const QList<QAction *> offered = document->contextMenuActions(cursor);
        QCOMPARE(offered.size(), 1);
        QAction * const refactoring = offered.first();
        QVERIFY2(refactoring->menu(), "the Refactoring entry carries no menu");
        QTRY_VERIFY2(!refactoring->menu()->isEmpty(),
                     "no quick fix was offered where one applies");
        QVERIFY2(refactoring->isEnabled(),
                 "the Refactoring entry was left disabled with entries in it");
        const QStringList entries = Utils::transform(refactoring->menu()->actions(),
                                                     [](QAction *a) { return a->text(); });
        QVERIFY2(entries.contains("Split Initializer"), qPrintable(entries.join(", ")));

        // Asked where nothing applies, the menu is empty - so the assertion
        // above is about this cursor rather than about the file.
        QTextCursor atImport(document->document());
        atImport.setPosition(2);
        TextEditor::setTextCursorOf(editor, atImport);
        const QList<QAction *> nothing = document->contextMenuActions(atImport);
        QCOMPARE(nothing.size(), 1);
        QVERIFY2(nothing.first()->menu()->isEmpty(),
                 "a quick fix was offered on the import line");
    }

    // The QML entries a right click offers. QmlJSEditorWidget names its
    // container in contextMenuEvent(), which a view that is not a widget
    // cannot do - it reads the one the factory names instead.
    void testTheFactoryNamesTheQmlContextMenu()
    {
        Utils::TemporaryDirectory dir("qmljs-context-menu");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("thing.qml");
        QVERIFY(file.writeFileContents("import QtQuick\nItem {}\n"));

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        QCOMPARE(factory->contextMenuId(), Utils::Id(QmlJSEditor::Constants::M_CONTEXT));

        // And the container it names has entries, or naming it would achieve
        // nothing.
        Core::ActionContainer * const container
            = Core::ActionManager::actionContainer(QmlJSEditor::Constants::M_CONTEXT);
        QVERIFY2(container && container->menu(), "the QML context menu container does not exist");
        QVERIFY2(!container->menu()->actions().isEmpty(),
                 "the QML context menu container is empty");
    }

    // Find Usages and Rename Symbol were QmlJSEditorWidget virtuals, so a QML
    // file open in anything else could not be asked either. A view that is not
    // a widget asks through a relay; this checks the factory wires it up.
    void testFindUsagesAndRenameReachAViewThatIsNotAWidget()
    {
        Utils::TemporaryDirectory dir("qmljs-symbols");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("thing.qml");
        QVERIFY(file.writeFileContents("import QtQuick\nItem { id: root }\n"));

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the QML file opened in a widget editor, so this tests nothing");

        // The relay exists for this view...
        TextEditor::SymbolRequests * const requests
            = TextEditor::symbolRequestsForEditor(editor);
        QVERIFY2(requests, "the view offers no relay to ask through");

        // ...and the language answered it. Without the decorator this editor
        // has no FindReferences of its own and nothing is listening.
        QVERIFY2(editor->findChild<FindReferences *>(),
                 "the factory wired nothing up for this editor");
    }

    // The parse errors used to be put on the editor widget, so a QML file
    // open in anything else underlined nothing. They are the document's now.
    void testParseErrorsAreOnTheDocument()
    {
        Utils::TemporaryDirectory dir("qmljs-warnings");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("broken.qml");
        // Unbalanced, so there is no AST at all - which is the branch that
        // reports parse errors rather than semantic ones.
        QVERIFY(file.writeFileContents("import QtQuick\nItem { width: }\n"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "nothing opened a .qml file");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const document = qobject_cast<QmlJSEditorDocument *>(editor->document());
        QVERIFY2(document, "a .qml file did not open with a QmlJSEditorDocument");

        // On the document, which is what anything showing the file can read -
        // asked of the document rather than of the widget on purpose.
        QTRY_VERIFY2(!document->extraSelections(
                          TextEditor::TextEditorWidget::CodeWarningsSelection).isEmpty(),
                     "a QML file that does not parse was given no warnings");

        // And a file that parses is left alone, so the assertion above is not
        // true of every QML file.
        const Utils::FilePath fine = dir.filePath("fine.qml");
        QVERIFY(fine.writeFileContents("import QtQuick\nItem { width: 1 }\n"));
        Core::IEditor * const good = Core::EditorManager::openEditor(fine);
        QVERIFY(good);
        const QScopeGuard closeGood(
            [good] { Core::EditorManager::closeEditors({good}, false); });
        auto * const fineDocument = qobject_cast<QmlJSEditorDocument *>(good->document());
        QVERIFY(fineDocument);
        QVERIFY2(fineDocument->extraSelections(
                     TextEditor::TextEditorWidget::CodeWarningsSelection).isEmpty(),
                 "a QML file that parses was given warnings");
    }

    // Completion used to be answered by QmlJSEditorWidget, so a QML file open
    // in anything else got the plain text proposals. TextDocument declares
    // createAssistInterface() virtual for exactly this, and it is what a view
    // that is not a widget asks.
    void testCompletionIsAnsweredByTheDocument()
    {
        QmlJSEditorDocument document(Utils::Id("QmlJSEditor.QMLJSEditor"));
        document.setMimeType(Utils::Constants::QML_MIMETYPE);
        document.document()->setPlainText("import QtQuick\nItem { width: 1 }\n");

        QTextCursor cursor(document.document());
        cursor.setPosition(document.plainText().indexOf("width") + 5);

        const std::unique_ptr<TextEditor::AssistInterface> interface
            = document.createAssistInterface(cursor, TextEditor::Completion,
                                             TextEditor::ExplicitlyInvoked, nullptr);
        QVERIFY2(interface, "the document proposes nothing at all");
        auto * const qml = dynamic_cast<QmlJSCompletionAssistInterface *>(interface.get());
        QVERIFY2(qml, "the document answered with the plain text interface");
        QCOMPARE(qml->cursor().position(), cursor.position());

        // Quick fixes too, and with the language's own interface rather than
        // the base's - the refactoring file it carries is what an operation
        // edits through.
        const std::unique_ptr<TextEditor::AssistInterface> quickFix
            = document.createAssistInterface(cursor, TextEditor::QuickFix,
                                             TextEditor::ExplicitlyInvoked, nullptr);
        QVERIFY(quickFix);
        QVERIFY2(!dynamic_cast<QmlJSCompletionAssistInterface *>(quickFix.get()),
                 "a quick fix was answered with the completion interface");
        auto * const fix
            = dynamic_cast<Internal::QmlJSQuickFixAssistInterface *>(quickFix.get());
        QVERIFY2(fix, "the document answered a quick fix with the plain text interface");
        QVERIFY2(fix->currentFile(), "the quick fix interface carries no file to edit");
        QCOMPARE(fix->currentFile()->filePath(), document.filePath());

        // And a kind neither of them handles is still the base's.
        const std::unique_ptr<TextEditor::AssistInterface> hint
            = document.createAssistInterface(cursor, TextEditor::FunctionHint,
                                             TextEditor::ExplicitlyInvoked, nullptr);
        QVERIFY(hint);
        QVERIFY2(!dynamic_cast<Internal::QmlJSQuickFixAssistInterface *>(hint.get()),
                 "a function hint was answered with the quick fix interface");
    }
};

QObject *createQmlJSEditorDocumentTest()
{
    return new QmlJSEditorDocumentTest;
}

#endif // WITH_TESTS

TextEditor::IAssistProvider *QmlJSEditorDocument::quickFixAssistProvider() const
{
    if (const auto baseProvider = TextDocument::quickFixAssistProvider())
        return baseProvider;
    return Internal::quickFixAssistProvider();
}

void QmlJSEditorDocument::setIsDesignModePreferred(bool value)
{
    d->m_isDesignModePreferred = value;
    if (value) {
        if (infoBar()->canInfoBeAdded(QML_UI_FILE_WARNING)) {
            InfoBarEntry info(QML_UI_FILE_WARNING,
                              Tr::tr("This file should only be edited in <b>Design</b> mode."));
            info.addCustomButton(Tr::tr("Switch Mode"), []() {
                Core::ModeManager::activateMode(Core::Constants::MODE_DESIGN);
            });
            infoBar()->addInfo(info);
        }
    } else if (infoBar()->containsInfo(QML_UI_FILE_WARNING)) {
        infoBar()->removeInfo(QML_UI_FILE_WARNING);
    }
}

bool QmlJSEditorDocument::isDesignModePreferred() const
{
    return d->m_isDesignModePreferred;
}

void QmlJSEditorDocument::setDiagnosticRanges(const QVector<QTextLayout::FormatRange> &ranges)
{
    d->m_diagnosticRanges = ranges;
}

void QmlJSEditorDocument::applyFontSettings()
{
    TextDocument::applyFontSettings();
    d->m_semanticHighlighter->updateFontSettings(fontSettings());
    if (!isSemanticInfoOutdated()) {
        d->m_semanticHighlightingNecessary = false;
        d->m_semanticHighlighter->rerun(d->m_semanticInfo);
    }
}

void QmlJSEditorDocument::triggerPendingUpdates()
{
    TextDocument::triggerPendingUpdates(); // calls applyFontSettings if necessary
    // might still need to rehighlight if font settings did not change
    if (d->m_semanticHighlightingNecessary && !isSemanticInfoOutdated()) {
        d->m_semanticHighlightingNecessary = false;
        d->m_semanticHighlighter->rerun(d->m_semanticInfo);
    }
    if (d->m_outlineModelNeedsUpdate && !isSemanticInfoOutdated()) {
        d->m_outlineModelNeedsUpdate = false;
        d->m_updateOutlineModelTimer.start();
    }
}

void QmlJSEditorDocument::setSourcesWithCapabilities(
    const LanguageServerProtocol::ServerCapabilities &cap)
{
    d->setSourcesWithCapabilities(cap);
}



} // QmlJSEditor

#include "qmljseditordocument.moc"

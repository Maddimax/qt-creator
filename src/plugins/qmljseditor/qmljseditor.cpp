// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmljseditor.h"

#include "qmljsautocompleter.h"
#include "qmljscompletionassist.h"
#include "qmljseditorconstants.h"
#include "qmljseditordocument.h"
#include "qmljseditorplugin.h"
#include "qmljseditorsettings.h"
#include "qmljseditortr.h"
#include "qmljsfindreferences.h"
#include "qmljshighlighter.h"
#include "qmljshoverhandler.h"
#include "qmljsquickfixassist.h"
#include "qmloutlinemodel.h"
#include "quicktoolbar.h"

#include <qmljs/qmljsbind.h>
#include <qmljs/qmljsevaluate.h>
#include <qmljs/qmljsmodelmanagerinterface.h>
#include <qmljs/qmljsutils.h>

#include <qmljstools/qmljsindenter.h>
#include <qmljstools/qmljssettings.h>
#include <qmljstools/qmljstoolsconstants.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/resourcepreviewhoverhandler.h>

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/designmode.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>

#include <extensionsystem/pluginmanager.h>

#include <texteditor/codeassist/genericproposalmodel.h>
#include <texteditor/codeassist/iassistprocessor.h>
#include <texteditor/codeassist/iassistproposal.h>
#include <texteditor/colorpreviewhoverhandler.h>
#include <texteditor/fontsettings.h>
#include <texteditor/refactoroverlay.h>
#include <texteditor/snippets/snippetprovider.h>
#include <texteditor/syntaxhighlighter.h>
#include <texteditor/symbolrequests.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditorconstants.h>
#include <texteditor/textmark.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/changeset.h>
#include <utils/delegates.h>
#include <utils/mimeconstants.h>
#include <utils/qtcassert.h>
#include <utils/treeviewcombobox.h>
#include <utils/uncommentselection.h>

#include <languageclient/languageclientmanager.h>
#include <languageclient/locatorfilter.h>
#include <languageclient/languageclientsymbolsupport.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QHeaderView>
#include <QMenu>
#include <QMetaMethod>
#include <QPointer>
#include <QScopedPointer>
#include <QTimer>
#include <QTreeView>
#include <QDebug>

enum {
    UPDATE_USES_DEFAULT_INTERVAL = 150,
    UPDATE_OUTLINE_INTERVAL = 500 // msecs after new semantic info has been arrived / cursor has moved
};

const char QML_JS_EDITOR_PLUGIN[] = "QmlJSEditorPlugin";
const char QT_QUICK_TOOLBAR_MARKER_ID[] = "QtQuickToolbarMarkerId";

using namespace Core;
using namespace QmlJS;
using namespace QmlJS::AST;
using namespace QmlJSEditor::Internal;
using namespace QmlJSTools;
using namespace TextEditor;
using namespace Utils;

namespace QmlJSEditor {

// Whether the Qt Quick helper has anything to show for the element covering
// \a position. The document's parse and a position in it, and nothing else -
// QuickToolBar::isAvailable() took an editor widget and never read it, which
// is the only reason asking used to be a widget's privilege.
static bool quickHelperIsAvailableAt(QmlJSEditorDocument *document, int position)
{
    if (!document)
        return false;
    const QmlJSTools::SemanticInfo info = document->semanticInfo();
    return QuickToolBar::isAvailable(info.document,
                                     info.declaringMemberNoProperties(position));
}

bool quickHelperIsAvailableIn(Core::IEditor *editor)
{
    if (!editor)
        return false;
    return quickHelperIsAvailableAt(qobject_cast<QmlJSEditorDocument *>(editor->document()),
                                    TextEditor::textCursorOf(editor).position());
}
//
// QmlJSEditorWidget
//

QmlJSEditorWidget::QmlJSEditorWidget()
{
    m_findReferences = new FindReferences(this);
    setLanguageSettingsId(QmlJSTools::Constants::QML_JS_SETTINGS_ID);

    connect(this, &QmlJSEditorWidget::toolbarOutlineChanged,
            this, &QmlJSEditorWidget::updateOutline);
}

void QmlJSEditorWidget::finalizeInitialization()
{
    m_updateOutlineIndexTimer.setInterval(UPDATE_OUTLINE_INTERVAL);
    m_updateOutlineIndexTimer.setSingleShot(true);
    connect(&m_updateOutlineIndexTimer, &QTimer::timeout,
            this, &QmlJSEditorWidget::updateOutlineIndexNow);

    m_contextPane = QuickToolBar::instance();

    m_contextPaneTimer.setInterval(UPDATE_OUTLINE_INTERVAL);
    m_contextPaneTimer.setSingleShot(true);
    connect(&m_contextPaneTimer, &QTimer::timeout, this, &QmlJSEditorWidget::updateContextPane);
    if (m_contextPane) {
        connect(this, &QmlJSEditorWidget::cursorPositionChanged,
                &m_contextPaneTimer, QOverload<>::of(&QTimer::start));
        connect(m_contextPane, &QuickToolBar::closed, this, &QmlJSEditorWidget::showTextMarker);
    }

    connect(qmlJsEditorDocument(), &QmlJSEditorDocument::semanticInfoUpdated,
            this, &QmlJSEditorWidget::semanticInfoUpdated);

    setRequestMarkEnabled(true);
    createToolBar();
}

void QmlJSEditorWidget::restoreState(const QByteArray &state)
{
    using namespace Utils::Constants;
    QStringList qmlTypes = {QML_MIMETYPE, QBS_MIMETYPE, QMLTYPES_MIMETYPE, QMLUI_MIMETYPE};

    if (settings().foldAuxData() && qmlTypes.contains(textDocument()->mimeType())) {
        int version = 0;
        QDataStream stream(state);
        stream >> version;
        // A state written before this was remembered. An empty one goes
        // through foldOnFirstOpen() on the document instead.
        if (version < 1 && !state.isEmpty())
            qmlJsEditorDocument()->foldAuxiliaryData();
    }

    TextEditorWidget::restoreState(state);
}

QModelIndex QmlJSEditorWidget::outlineModelIndex()
{
    if (!m_outlineModelIndex.isValid()) {
        m_outlineModelIndex = qmlJsEditorDocument()->outlineModel()->indexForPosition(position());
    }
    return m_outlineModelIndex;
}


void QmlJSEditorWidget::jumpToOutlineElement(int /*index*/)
{
    if (!m_outlineCombo)
        return;
    QModelIndex index = m_outlineCombo->view()->currentIndex();
    SourceLocation location = qmlJsEditorDocument()->outlineModel()->sourceLocation(index);

    if (!location.isValid())
        return;

    EditorManager::cutForwardNavigationHistory();
    EditorManager::addCurrentPositionToNavigationHistory();

    QTextCursor cursor = textCursor();
    cursor.setPosition(location.offset);
    setTextCursor(cursor);

    setFocus();
}

void QmlJSEditorWidget::updateOutlineIndexNow()
{
    if (!m_outlineCombo)
        return;
    if (!qmlJsEditorDocument()->outlineModel()->document())
        return;

    if (qmlJsEditorDocument()->outlineModel()->document()->editorRevision() != document()->revision()) {
        m_updateOutlineIndexTimer.start();
        return;
    }

    m_outlineModelIndex = QModelIndex(); // invalidate
    const QModelIndex comboIndex = outlineModelIndex();
    if (comboIndex.isValid()) {
        QSignalBlocker blocker(m_outlineCombo);
        m_outlineCombo->setCurrentIndex(comboIndex);
    }
}

void QmlJSEditorWidget::updateContextPane()
{
    const SemanticInfo info = qmlJsEditorDocument()->semanticInfo();
    if (m_contextPane && document() && info.isValid()
            && document()->revision() == info.document->editorRevision())
    {
        Node *oldNode = info.declaringMemberNoProperties(m_oldCursorPosition);
        Node *newNode = info.declaringMemberNoProperties(position());
        if (oldNode != newNode && m_oldCursorPosition != -1)
            m_contextPane->apply(this, info.document, nullptr, newNode, false);

        if (QuickToolBar::isAvailable(info.document, newNode) &&
            !m_contextPane->widget()->isVisible()) {
            RefactorMarkers markers;
            if (UiObjectMember *m = newNode->uiObjectMemberCast()) {
                const int start = qualifiedTypeNameId(m)->identifierToken.begin();
                for (UiQualifiedId *q = qualifiedTypeNameId(m); q; q = q->next) {
                    if (! q->next) {
                        const int end = q->identifierToken.end();
                        if (position() >= start && position() <= end) {
                            RefactorMarker marker;
                            QTextCursor tc(document());
                            tc.setPosition(end);
                            marker.cursor = tc;
                            marker.tooltip = Tr::tr("Show Qt Quick ToolBar");
                            marker.type = QT_QUICK_TOOLBAR_MARKER_ID;
                            marker.callback = [this](Core::IEditor *) {
                                showContextPane();
                            };
                            markers.append(marker);
                        }
                    }
                }
            }
            setRefactorMarkers(markers, QT_QUICK_TOOLBAR_MARKER_ID);
        } else if (oldNode != newNode) {
            clearRefactorMarkers(QT_QUICK_TOOLBAR_MARKER_ID);
        }
        m_oldCursorPosition = position();

        setSelectedElements();
    }
}

void QmlJSEditorWidget::showTextMarker()
{
    m_oldCursorPosition = -1;
    updateContextPane();
}

class SelectedElement: protected Visitor
{
    unsigned m_cursorPositionStart = 0;
    unsigned m_cursorPositionEnd = 0;
    QList<UiObjectMember *> m_selectedMembers;

public:
    QList<UiObjectMember *> operator()(const Document::Ptr &doc, unsigned startPosition, unsigned endPosition)
    {
        m_cursorPositionStart = startPosition;
        m_cursorPositionEnd = endPosition;
        m_selectedMembers.clear();
        Node::accept(doc->qmlProgram(), this);
        return m_selectedMembers;
    }

protected:

    bool isSelectable(UiObjectMember *member) const
    {
        UiQualifiedId *id = qualifiedTypeNameId(member);
        if (id) {
            QStringView name = id->name;
            if (!name.isEmpty() && name.at(0).isUpper())
                return true;
        }

        return false;
    }

    inline bool isIdBinding(UiObjectMember *member) const
    {
        if (auto script = cast<const UiScriptBinding *>(member)) {
            if (!script->qualifiedId || script->qualifiedId->name.isEmpty()
                || script->qualifiedId->next) {
                return false;
            }

            QStringView propertyName = script->qualifiedId->name;

            if (propertyName == QLatin1String("id"))
                return true;
        }

        return false;
    }

    inline bool containsCursor(unsigned begin, unsigned end)
    {
        return m_cursorPositionStart >= begin && m_cursorPositionEnd <= end;
    }

    inline bool intersectsCursor(unsigned begin, unsigned end)
    {
        return (m_cursorPositionEnd >= begin && m_cursorPositionStart <= end);
    }

    inline bool isRangeSelected() const
    {
        return (m_cursorPositionStart != m_cursorPositionEnd);
    }

    void postVisit(Node *ast) override
    {
        if (!isRangeSelected() && !m_selectedMembers.isEmpty())
            return; // nothing to do, we already have the results.

        if (UiObjectMember *member = ast->uiObjectMemberCast()) {
            unsigned begin = member->firstSourceLocation().begin();
            unsigned end = member->lastSourceLocation().end();

            if ((isRangeSelected() && intersectsCursor(begin, end))
            || (!isRangeSelected() && containsCursor(begin, end)))
            {
                if (initializerOfObject(member) && isSelectable(member)) {
                    m_selectedMembers << member;
                    // move start towards end; this facilitates multiselection so that root is usually ignored.
                    m_cursorPositionStart = qMin(end, m_cursorPositionEnd);
                }
            }
        }
    }

    void throwRecursionDepthError() override
    {
        qWarning("Warning: Hit maximum recursion depth visiting AST in SelectedElement");
    }
};

void QmlJSEditorWidget::setSelectedElements()
{
    static const QMetaMethod selectedChangedSignal =
            QMetaMethod::fromSignal(&QmlJSEditorWidget::selectedElementsChanged);
    if (!isSignalConnected(selectedChangedSignal))
        return;

    QTextCursor tc = textCursor();
    QString wordAtCursor;
    QList<UiObjectMember *> offsets;

    unsigned startPos;
    unsigned endPos;

    if (tc.hasSelection()) {
        startPos = tc.selectionStart();
        endPos = tc.selectionEnd();
    } else {
        tc.movePosition(QTextCursor::StartOfWord);
        tc.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);

        startPos = textCursor().position();
        endPos = textCursor().position();
    }

    if (qmlJsEditorDocument()->semanticInfo().isValid()) {
        SelectedElement selectedMembers;
        const QList<UiObjectMember *> members
            = selectedMembers(qmlJsEditorDocument()->semanticInfo().document, startPos, endPos);
        if (!members.isEmpty()) {
            for (UiObjectMember *m : members) {
                offsets << m;
            }
        }
    }
    wordAtCursor = tc.selectedText();

    emit selectedElementsChanged(offsets, wordAtCursor);
}

void QmlJSEditorWidget::createToolBar()
{
    m_outlineCombo = new Utils::TreeViewComboBox;
    m_outlineCombo->setMinimumContentsLength(22);
    m_outlineCombo->setModel(qmlJsEditorDocument()->outlineModel());

    auto itemDelegate = new Utils::AnnotatedItemDelegate(this);
    itemDelegate->setDelimiter(QLatin1String(" "));
    itemDelegate->setAnnotationRole(Internal::QmlOutlineModel::AnnotationRole);

    QTreeView *treeView = m_outlineCombo->view();
    treeView->setItemDelegateForColumn(0, itemDelegate);
    treeView->setItemsExpandable(false);
    treeView->expandAll();

    //m_outlineCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    // Make the combo box prefer to expand
    QSizePolicy policy = m_outlineCombo->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    m_outlineCombo->setSizePolicy(policy);

    connect(m_outlineCombo, &QComboBox::activated,
            this, &QmlJSEditorWidget::jumpToOutlineElement);
    connect(qmlJsEditorDocument()->outlineModel(), &Internal::QmlOutlineModel::updated,
            m_outlineCombo->view(), &QTreeView::expandAll);

    connect(this, &QmlJSEditorWidget::cursorPositionChanged,
            &m_updateOutlineIndexTimer, QOverload<>::of(&QTimer::start));

    setToolbarOutline(m_outlineCombo);
}

void QmlJSEditorWidget::updateOutline(QWidget *newOutline)
{
    if (!newOutline) {
        createToolBar();
    } else if (newOutline != m_outlineCombo){
        m_outlineCombo = nullptr;
    }
}

class CodeModelInspector : public MemberProcessor
{
public:
    explicit CodeModelInspector(const CppComponentValue *processingValue, QTextStream *stream) :
        m_processingValue(processingValue),
        m_stream(stream),
        m_indent(QLatin1String("    "))
    {
    }

    bool processProperty(const QString &name, const Value *value,
                                 const PropertyInfo &propertyInfo) override
    {
        QString type;
        if (const CppComponentValue *cpp = value->asCppComponentValue())
            type = cpp->metaObject()->className();
        else
            type = m_processingValue->propertyType(name);

        if (propertyInfo.isList())
            type = QStringLiteral("list<%1>").arg(type);

        *m_stream << m_indent;
        if (!propertyInfo.isWriteable())
            *m_stream << "readonly ";
        *m_stream << "property " << type << " " << name << '\n';

        return true;
    }
    bool processSignal(const QString &name, const Value *value) override
    {
        *m_stream << m_indent << "signal " << name << stringifyFunctionParameters(value) << '\n';
        return true;
    }
    bool processSlot(const QString &name, const Value *value) override
    {
        *m_stream << m_indent << "function " << name << stringifyFunctionParameters(value) << '\n';
        return true;
    }
    bool processGeneratedSlot(const QString &name, const Value *value) override
    {
        *m_stream << m_indent << "/*generated*/ function " << name
                  << stringifyFunctionParameters(value) << '\n';
        return true;
    }

private:
    QString stringifyFunctionParameters(const Value *value) const
    {
        QStringList params;
        const QmlJS::MetaFunction *metaFunction = value->asMetaFunction();
        if (metaFunction) {
            QStringList paramNames = metaFunction->fakeMetaMethod().parameterNames();
            QStringList paramTypes = metaFunction->fakeMetaMethod().parameterTypes();
            for (int i = 0; i < paramTypes.size(); ++i) {
                QString typeAndNamePair = paramTypes.at(i);
                if (paramNames.size() > i) {
                    QString paramName = paramNames.at(i);
                    if (!paramName.isEmpty())
                        typeAndNamePair += QLatin1Char(' ') + paramName;
                }
                params.append(typeAndNamePair);
            }
        }
        return QLatin1Char('(') + params.join(QLatin1String(", ")) + QLatin1Char(')');
    }

private:
    const CppComponentValue *m_processingValue;
    QTextStream *m_stream;
    const QString m_indent;
};

static const CppComponentValue *findCppComponentToInspect(const SemanticInfo &semanticInfo,
                                                          const unsigned cursorPosition)
{
    AST::Node *node = semanticInfo.astNodeAt(cursorPosition);
    if (!node)
        return nullptr;

    const ScopeChain scopeChain = semanticInfo.scopeChain(semanticInfo.rangePath(cursorPosition));
    Evaluate evaluator(&scopeChain);
    const Value *value = evaluator.reference(node);
    if (!value)
        return nullptr;

    return value->asCppComponentValue();
}

static QString inspectCppComponent(const CppComponentValue *cppValue)
{
    QString result;
    QTextStream bufWriter(&result);

    // for QtObject
    QString superClassName = cppValue->metaObject()->superclassName();
    if (superClassName.isEmpty())
        superClassName = cppValue->metaObject()->className();

    bufWriter << "import QtQuick " << cppValue->importVersion().toString() << '\n'
              << "// " << cppValue->metaObject()->className()
              << " imported as " << cppValue->moduleName()  << " "
              << cppValue->importVersion().toString() << '\n'
              << '\n'
              << superClassName << " {" << '\n';

    CodeModelInspector insp(cppValue, &bufWriter);
    cppValue->processMembers(&insp);

    bufWriter << '\n';
    const int enumeratorCount = cppValue->metaObject()->enumeratorCount();
    for (int index = cppValue->metaObject()->enumeratorOffset(); index < enumeratorCount; ++index) {
        LanguageUtils::FakeMetaEnum enumerator = cppValue->metaObject()->enumerator(index);
        bufWriter << "    enum " << enumerator.name() << " {" << '\n';
        const QStringList keys = enumerator.keys();
        const int keysCount = keys.size();
        for (int i = 0; i < keysCount; ++i) {
            bufWriter << "        " << keys.at(i);
            if (i != keysCount - 1)
                bufWriter << ',';
            bufWriter << '\n';
        }
        bufWriter << "    }" << '\n';
    }

    bufWriter << "}" << '\n';
    return result;
}

// The QML type under the caret, dumped into a read-only editor of its own.
// Asked of the editor rather than of a widget: what it needs is where the
// caret is and what the document's parse says is there, and both views answer.
static void inspectElementIn(Core::IEditor *editor)
{
    auto * const document = qobject_cast<QmlJSEditorDocument *>(editor->document());
    if (!document)
        return;

    const unsigned cursorPosition = TextEditor::textCursorOf(editor).position();
    const SemanticInfo semanticInfo = document->semanticInfo();
    if (!semanticInfo.isValid())
        return;

    const CppComponentValue *cppValue = findCppComponentToInspect(semanticInfo, cursorPosition);
    if (!cppValue) {
        QString title = Tr::tr("Code Model Not Available");
        const QString documentId = QML_JS_EDITOR_PLUGIN + QStringLiteral(".NothingToShow");
        EditorManager::openEditorWithContents(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID, &title,
                                              Tr::tr("Code model not available.").toUtf8(), documentId,
                                              EditorManager::IgnoreNavigationHistory);
        return;
    }

    QString title = Tr::tr("Code Model of %1").arg(cppValue->metaObject()->className());
    const QString documentId = QML_JS_EDITOR_PLUGIN + QStringLiteral(".Class.")
                               + cppValue->metaObject()->className();
    IEditor *outputEditor = EditorManager::openEditorWithContents(
                Core::Constants::K_DEFAULT_TEXT_EDITOR_ID, &title, QByteArray(),
                documentId, EditorManager::IgnoreNavigationHistory);

    if (!outputEditor)
        return;

    // The output is its own document, so it is configured as one - the view
    // that opened for it does not come into it.
    auto * const outputDocument = qobject_cast<TextEditor::TextDocument *>(
        outputEditor->document());
    if (!outputDocument)
        return;

    outputDocument->setTemporary(true);
    outputDocument->resetSyntaxHighlighter([] { return new QmlJSHighlighter(); });
    outputDocument->setPlainText(inspectCppComponent(cppValue));
    TextEditor::setReadOnlyIn(outputEditor, true);
}

// Where the name under the cursor comes from: an import, a property, an id or
// a type. The language server answers if there is one; otherwise the semantic
// info on the document does.
void findQmlJSLinkAt(TextEditor::TextDocument *textDocument,
                     const QTextCursor &cursor,
                     const Utils::LinkHandler &processLinkCallback,
                     bool resolveTarget,
                     bool /*inNextSplit*/)
{
    if (auto client = LanguageClient::LanguageClientManager::clientForFilePath(
            textDocument->filePath())) {
        client->findLinkAt(textDocument,
                           cursor,
                           processLinkCallback,
                           resolveTarget,
                           LanguageClient::LinkTarget::SymbolDef);
        return;
    }

    auto * const qmlDocument = qobject_cast<QmlJSEditorDocument *>(textDocument);
    QTC_ASSERT(qmlDocument, return);
    const SemanticInfo semanticInfo = qmlDocument->semanticInfo();
    if (! semanticInfo.isValid())
        return processLinkCallback(Utils::Link());

    const unsigned cursorPosition = cursor.position();

    AST::Node *node = semanticInfo.astNodeAt(cursorPosition);
    QTC_ASSERT(node, return;);

    if (auto importAst = cast<const AST::UiImport *>(node)) {
        // if it's a file import, link to the file
        const QList<ImportInfo> imports = semanticInfo.document->bind()->imports();
        for (const ImportInfo &import : imports) {
            if (import.ast() == importAst && import.type() == ImportType::File) {
                Utils::Link link(
                    ModelManagerInterface::instance()->fileToSource(FilePath::fromString(import.path())));
                link.linkTextStart = importAst->firstSourceLocation().begin();
                link.linkTextEnd = importAst->lastSourceLocation().end();
                processLinkCallback(Utils::Link());
                return;
            }
        }
        processLinkCallback(Utils::Link());
        return;
    }

    const ProjectExplorer::Project * const project = ProjectExplorer::ProjectTree::currentProject();
    ProjectExplorer::ProjectNode* projectRootNode = nullptr;
    if (project) {
        projectRootNode = project->rootProjectNode();
    }

    // string literals that could refer to a file link to them
    if (auto literal = cast<const StringLiteral *>(node)) {
        const QString &text = literal->value.toString();
        if (text.startsWith("qrc:/")) {
            if (projectRootNode) {
                const ProjectExplorer::Node * const nodeForPath = projectRootNode->findNode(
                            [qrcPath = text.mid(text.indexOf(':') + 1)](ProjectExplorer::Node *n) {
                    if (!n->asFileNode())
                        return false;
                    const auto qrcNode = dynamic_cast<ProjectExplorer::ResourceFileNode *>(n);
                    return qrcNode && qrcNode->qrcPath() == qrcPath;
                });
                if (nodeForPath) {
                    Link link(nodeForPath->filePath());
                    link.linkTextStart = literal->firstSourceLocation().begin();
                    link.linkTextEnd = literal->lastSourceLocation().end();
                    processLinkCallback(link);
                    return;
                }
            }
        }

        if (text.startsWith("https:/") || text.startsWith("http:/")) {
            Link link = Link::fromString(text);
            link.linkTextStart = literal->literalToken.begin();
            link.linkTextEnd = literal->literalToken.end();
            processLinkCallback(link);
            return;
        }

        Utils::Link link;
        link.linkTextStart = literal->literalToken.begin();
        link.linkTextEnd = literal->literalToken.end();
        Utils::FilePath targetFilePath = Utils::FilePath::fromUserInput(text);
        if (semanticInfo.snapshot.document(targetFilePath)) {
            link.targetFilePath = targetFilePath;
            processLinkCallback(link);
            return;
        }
        const Utils::FilePath relative = semanticInfo.document->path().pathAppended(text);
        if (relative.exists()) {
            link.targetFilePath = ModelManagerInterface::instance()->fileToSource(relative);
            processLinkCallback(link);
            return;
        }
    }

    const ScopeChain scopeChain = semanticInfo.scopeChain(semanticInfo.rangePath(cursorPosition));
    Evaluate evaluator(&scopeChain);
    const Value *value = evaluator.reference(node);

    Utils::FilePath fileName;
    int line = 0, column = 0;

    if (! (value && value->getSourceLocation(&fileName, &line, &column)))
        return processLinkCallback(Utils::Link());

    Utils::Link link;
    link.targetFilePath = ModelManagerInterface::instance()->fileToSource(fileName);
    link.target.line = line;
    link.target.column = column - 1; // adjust the column

    auto processPotentialCppLink = [&]() -> bool {
        if (!value->asCppComponentValue() || !projectRootNode) {
            processLinkCallback(link);
            return true;
        }

        const ProjectExplorer::Node * const nodeForPath = projectRootNode->findNode(
            [&fileName](ProjectExplorer::Node *n) {
                const auto fileNode = n->asFileNode();
                if (!fileNode)
                    return false;
                Utils::FilePath filePath = n->filePath();
                return filePath.endsWith(fileName.toUserOutput());
            });
        if (nodeForPath) {
            link.targetFilePath = nodeForPath->filePath();
            processLinkCallback(link);
            return true;
        }

        // else we will process an empty link below to avoid an error dialog
        return false;
    };

    if (auto q = AST::cast<const AST::UiQualifiedId *>(node)) {
        for (const AST::UiQualifiedId *tail = q; tail; tail = tail->next) {
            if (tail->next || !(cursorPosition <= tail->identifierToken.end())) {
                continue;
            }

            link.linkTextStart = tail->identifierToken.begin();
            link.linkTextEnd = tail->identifierToken.end();

            if (processPotentialCppLink()) {
                return;
            }
        }
    } else if (auto id = AST::cast<const AST::IdentifierExpression *>(node)) {
        link.linkTextStart = id->firstSourceLocation().begin();
        link.linkTextEnd = id->lastSourceLocation().end();

        if (processPotentialCppLink()) {
            return;
        }

    } else if (auto mem = AST::cast<const AST::FieldMemberExpression *>(node)) {
        link.linkTextStart = mem->lastSourceLocation().begin();
        link.linkTextEnd = mem->lastSourceLocation().end();
        processLinkCallback(link);
        return;
    }

    processLinkCallback(Utils::Link());
}

// Find Usages and Rename Symbol, for a document and a place in it. The
// language server answers where there is one; the built-in model otherwise.
// Neither question is about the view asking it, which is why these take no
// editor and no widget.
void findQmlJSUsages(TextEditor::TextDocument *document, const QTextCursor &cursor,
                     FindReferences *findReferences)
{
    QTC_ASSERT(document && findReferences, return);
    const Utils::FilePath fileName = document->filePath();
    if (auto client = LanguageClient::LanguageClientManager::clientForFilePath(fileName))
        client->symbolSupport().findUsages(document, cursor);
    else
        findReferences->findUsages(fileName, cursor.position());
}

void renameQmlJSSymbol(TextEditor::TextDocument *document, const QTextCursor &cursor,
                       FindReferences *findReferences)
{
    QTC_ASSERT(document && findReferences, return);
    const Utils::FilePath fileName = document->filePath();
    if (auto client = LanguageClient::LanguageClientManager::clientForFilePath(fileName)) {
        QTextCursor word = cursor;
        word.select(QTextCursor::WordUnderCursor);
        client->symbolSupport().renameSymbol(document, cursor, word.selectedText());
    } else {
        findReferences->renameUsages(fileName, cursor.position());
    }
}

void QmlJSEditorWidget::findUsages()
{
    findQmlJSUsages(textDocument(), textCursor(), m_findReferences);
}

void QmlJSEditorWidget::renameSymbolUnderCursor()
{
    renameQmlJSSymbol(textDocument(), textCursor(), m_findReferences);
}

void QmlJSEditorWidget::showContextPane()
{
    const SemanticInfo info = qmlJsEditorDocument()->semanticInfo();
    if (m_contextPane && info.isValid()) {
        Node *newNode = info.declaringMemberNoProperties(position());
        ScopeChain scopeChain = info.scopeChain(info.rangePath(position()));
        m_contextPane->apply(this, info.document,
                             &scopeChain,
                             newNode, false, true);
        m_oldCursorPosition = position();
        clearRefactorMarkers(QT_QUICK_TOOLBAR_MARKER_ID);
    }
}

void QmlJSEditorWidget::contextMenuEvent(QContextMenuEvent *e)
{
    QPointer<QMenu> menu(new QMenu(this));

    QMenu *refactoringMenu = new QMenu(Tr::tr("Refactoring"), menu);

    if (!qmlJsEditorDocument()->isSemanticInfoOutdated()) {
        std::unique_ptr<AssistInterface> interface = textDocument()->createAssistInterface(
            textCursor(), QuickFix, ExplicitlyInvoked, nullptr);
        if (interface) {
            IAssistProcessor *processor = textDocument()->quickFixAssistProvider()->createProcessor(
                interface.get());
            auto handleProposal = [refactoringMenu = QPointer(refactoringMenu), processor](
                                      IAssistProposal *proposal) {
                QScopedPointer<IAssistProposal> proposalHolder(proposal);
                QScopedPointer<IAssistProcessor> processorHolder(processor);

                if (!refactoringMenu)
                    return;

                if (proposal) {
                    GenericProposalModelPtr model = proposal->model().staticCast<GenericProposalModel>();
                    for (int index = 0; index < model->size(); ++index) {
                        auto item = static_cast<const AssistProposalItem *>(
                            model->proposalItem(index));
                        QuickFixOperation::Ptr op = item->data().value<QuickFixOperation::Ptr>();
                        QAction *action = refactoringMenu->addAction(op->description());
                        connect(action, &QAction::triggered, refactoringMenu, [op]() {
                            op->perform();
                        });
                    }
                }
                refactoringMenu->setEnabled(!refactoringMenu->isEmpty());
            };

            if (IAssistProposal *proposal = processor->start(std::move(interface)))
                handleProposal(proposal);
            else
                processor->setAsyncCompletionAvailableHandler(handleProposal);
        }
    }

    refactoringMenu->setEnabled(!refactoringMenu->isEmpty());

    if (ActionContainer *mcontext = ActionManager::actionContainer(Constants::M_CONTEXT)) {
        QMenu *contextMenu = mcontext->menu();
        const QList<QAction *> actions = contextMenu->actions();
        for (QAction *action : actions) {
            menu->addAction(action);
            if (action->objectName() == QLatin1String(Constants::M_REFACTORING_MENU_INSERTION_POINT))
                menu->addMenu(refactoringMenu);
            if (action->objectName() == QLatin1String(Constants::SHOW_QT_QUICK_HELPER)) {
                action->setEnabled(
                    quickHelperIsAvailableAt(qmlJsEditorDocument(), position()));
            }
        }
    }

    appendStandardContextMenuActions(menu);

    menu->exec(e->globalPos());
    delete menu;
}

bool QmlJSEditorWidget::event(QEvent *e)
{
    switch (e->type()) {
    case QEvent::ShortcutOverride:
        if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape && m_contextPane) {
            if (hideContextPane()) {
                e->accept();
                return true;
            }
        }
        break;
    default:
        break;
    }

    return TextEditorWidget::event(e);
}


void QmlJSEditorWidget::wheelEvent(QWheelEvent *event)
{
    bool visible = false;
    if (m_contextPane && m_contextPane->widget()->isVisible())
        visible = true;

    TextEditorWidget::wheelEvent(event);

    if (visible)
        m_contextPane->apply(this, qmlJsEditorDocument()->semanticInfo().document, nullptr,
                             qmlJsEditorDocument()->semanticInfo().declaringMemberNoProperties(m_oldCursorPosition),
                             false, true);
}

void QmlJSEditorWidget::resizeEvent(QResizeEvent *event)
{
    TextEditorWidget::resizeEvent(event);
    hideContextPane();
}

 void QmlJSEditorWidget::scrollContentsBy(int dx, int dy)
 {
     TextEditorWidget::scrollContentsBy(dx, dy);
     hideContextPane();
 }

QmlJSEditorDocument *QmlJSEditorWidget::qmlJsEditorDocument() const
{
    return static_cast<QmlJSEditorDocument *>(textDocument());
}

void QmlJSEditorWidget::semanticInfoUpdated(const SemanticInfo &semanticInfo)
{
    if (isVisible()) {
         // trigger semantic highlighting and model update if necessary
        textDocument()->triggerPendingUpdates();
    }

    if (m_contextPane) {
        Node *newNode = semanticInfo.declaringMemberNoProperties(position());
        if (newNode) {
            m_contextPane->apply(this, semanticInfo.document, nullptr, newNode, true);
            m_contextPaneTimer.start(); //update text marker
        }
    }
}

bool QmlJSEditorWidget::hideContextPane()
{
    bool b = (m_contextPane) && m_contextPane->widget()->isVisible();
    if (b)
        m_contextPane->apply(this, qmlJsEditorDocument()->semanticInfo().document,
                             nullptr, nullptr, false);
    return b;
}

QString QmlJSEditorWidget::foldReplacementText(const QTextBlock &block) const
{
    const int curlyIndex = block.text().indexOf(QLatin1Char('{'));

    if (curlyIndex != -1 && qmlJsEditorDocument()->semanticInfo().isValid()) {
        const int pos = block.position() + curlyIndex;
        Node *node = qmlJsEditorDocument()->semanticInfo().rangeAt(pos);

        const QString objectId = idOfObject(node);
        if (!objectId.isEmpty())
            return QLatin1String("id: ") + objectId + QLatin1String("...");
    }

    return TextEditorWidget::foldReplacementText(block);
}


// What a QML file offers whichever formatter is configured. The two laying-out
// commands are not in here: which of them applies is the code style's answer
// and followTheFormatter() adds the one that does.
constexpr uint qmlJSFixedOptionalActions = OptionalActions::UnCommentSelection
                                           | OptionalActions::UnCollapseAll
                                           | OptionalActions::FollowSymbolUnderCursor
                                           | OptionalActions::RenameSymbol
                                           | OptionalActions::FindUsage;

// Which of Auto-indent and Auto-format a QML file offers. The builtin
// formatter indents and does not format; qmlformat formats and does not
// indent - so this is the code style's answer, not the language's, and it
// changes while a file is open. The editor widget narrowed the two commands
// by hand every time its context menu opened, which reached neither the menu
// bar nor a view that is not a widget.
static void followTheFormatter(Core::IEditor *editor)
{
    const auto apply = [editor] {
        const bool builtin = QmlJSTools::globalQmlJSCodeStyle()->currentCodeStyleSettings().formatter
                             == QmlJSTools::QmlJSCodeStyleSettings::Builtin;
        TextEditor::setOptionalActionsIn(editor,
                                         qmlJSFixedOptionalActions
                                             | (builtin ? OptionalActions::AutoIndentSelection
                                                        : OptionalActions::AutoFormatSelection));
    };
    QObject::connect(QmlJSTools::globalQmlJSCodeStyle(),
                     &TextEditor::ICodeStylePreferences::currentValueChanged, editor,
                     [apply] { apply(); });
    apply();
}

// Every other place the id under the caret appears, drawn in the occurrences
// colour. The editor widget kept this on itself, so a QML file in a view that
// is not one had the caret in an id and nothing else lit up.
class QmlJSUses final : public QObject
{
public:
    QmlJSUses(Core::IEditor *editor, QmlJSEditorDocument *document)
        : QObject(editor)
        , m_editor(editor)
        , m_document(document)
    {
        m_timer.setInterval(UPDATE_USES_DEFAULT_INTERVAL);
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, &QmlJSUses::update);
        // On a timer, because holding a cursor key walks over a word one
        // character at a time and each step would ask the model again.
        connect(editor, &Core::IEditor::cursorPositionChanged,
                &m_timer, QOverload<>::of(&QTimer::start));
        // A new parse replaces the locations these are read from, and the
        // colour they are drawn in is a setting the reader can change.
        connect(document, &QmlJSEditorDocument::semanticInfoUpdated,
                this, &QmlJSUses::update);
        connect(document, &TextEditor::TextDocument::fontSettingsChanged,
                this, &QmlJSUses::update);
    }

private:
    void update()
    {
        // The parse this reads is being replaced; semanticInfoUpdated brings
        // us back when the new one lands.
        if (m_document->isSemanticInfoOutdated())
            return;

        const QTextCharFormat format
            = m_document->fontSettings().toTextCharFormat(TextEditor::C_OCCURRENCES);
        // The code model does not hand these out in document order.
        const QList<SourceLocation> locations = Utils::sorted(
            m_document->semanticInfo().idLocations.value(wordUnderCaret()),
            [](const SourceLocation &lhs, const SourceLocation &rhs) {
                return lhs.begin() < rhs.begin();
            });

        QList<TextEditor::TextDocument::ExtraSelection> selections;
        for (const SourceLocation &location : locations) {
            if (!location.isValid())
                continue;
            QTextCursor cursor(m_document->document());
            cursor.setPosition(location.begin());
            cursor.setPosition(location.end(), QTextCursor::KeepAnchor);
            selections.append({cursor, format});
        }
        TextEditor::setViewSelections(m_editor,
                                      TextEditor::TextEditorWidget::CodeSemanticsSelection,
                                      selections);
    }

    QString wordUnderCaret() const
    {
        QTextCursor cursor = TextEditor::textCursorOf(m_editor);
        const QChar ch = m_document->document()->characterAt(cursor.position() - 1);
        // Make sure we are not at the start of the next word.
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_'))
            cursor.movePosition(QTextCursor::Left);
        cursor.movePosition(QTextCursor::StartOfWord);
        cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        return cursor.selectedText();
    }

    Core::IEditor * const m_editor;
    QmlJSEditorDocument * const m_document;
    QTimer m_timer;
};

//
// QmlJSEditorFactory
//

// Which element the caret is in, for the tool bar row to draw. The editor
// widget fills a combo box of its own; a view that is not one is handed this
// and draws it itself, the way CppEditorOutline is.
class QmlJSOutline final : public TextEditor::ToolBarOutline
{
public:
    QmlJSOutline(Core::IEditor *editor, QmlJSEditorDocument *document)
        : ToolBarOutline(editor)
        , m_editor(editor)
        , m_document(document)
    {}

    QAbstractItemModel *model() const override { return m_document->outlineModel(); }
    QModelIndex currentIndex() const override { return m_current; }

    QString currentText() const override
    {
        return m_current.isValid() ? m_current.data().toString() : QString();
    }

    void activate(const QModelIndex &index) override
    {
        const SourceLocation location = m_document->outlineModel()->sourceLocation(index);
        if (!location.isValid())
            return;

        // The reader asked to be taken somewhere, so Go Back returns here.
        EditorManager::cutForwardNavigationHistory();
        EditorManager::addCurrentPositionToNavigationHistory();

        QTextCursor cursor(m_document->document());
        cursor.setPosition(location.offset);
        TextEditor::setTextCursorOf(m_editor, cursor);
    }

    // Where the caret is now. Called when it moves and when the parse that
    // the model is built from is replaced.
    void updateIndex()
    {
        if (!m_document->outlineModel()->document())
            return;
        const QModelIndex index = m_document->outlineModel()->indexForPosition(
            TextEditor::textCursorOf(m_editor).position());
        if (index == m_current)
            return;
        m_current = index;
        emit currentIndexChanged();
    }

private:
    Core::IEditor * const m_editor;
    QmlJSEditorDocument * const m_document;
    QModelIndex m_current;
};

QmlJSEditorFactory::QmlJSEditorFactory()
    : QmlJSEditorFactory(Constants::C_QMLJSEDITOR_ID)
{}

QmlJSEditorFactory::QmlJSEditorFactory(Utils::Id _id)
{
    setId(_id);
    setDisplayName(Tr::tr("QMLJS Editor"));

    using namespace Utils::Constants;
    addMimeType(QML_MIMETYPE);
    addMimeType(QMLPROJECT_MIMETYPE);
    addMimeType(QMLTYPES_MIMETYPE);
    addMimeType(JS_MIMETYPE);

    setDocumentCreator([this]() { return new QmlJSEditorDocument(id()); });
    setEditorWidgetCreator([]() { return new QmlJSEditorWidget; });
    // The language this editor is for. Without this context the QML model
    // manager leaves the document out of its working copy and parses the file
    // on disk instead.
    addEditorContext(ProjectExplorer::Constants::QMLJS_LANGUAGE_ID);
    // A view that is not a widget asks through a relay rather than by being
    // asked itself. Same shape as C++: the editor gets its own FindReferences
    // and the two questions are answered by the free functions above.
    setEditorDecorator([](Core::IEditor *editor) {
        auto * const qmlDocument = qobject_cast<QmlJSEditorDocument *>(editor->document());
        // Which other ids in the file are this one. Not below the relay check:
        // both views draw these, through setViewSelections().
        if (qmlDocument) {
            new QmlJSUses(editor, qmlDocument);
            followTheFormatter(editor);
        }

        TextEditor::SymbolRequests * const requests
            = TextEditor::symbolRequestsForEditor(editor);
        // No relay means a widget editor, which owns the rest of this itself.
        if (!requests)
            return;

        // Which element the caret is in, which is what the tool bar row says.
        // Not below anything conditional: entry 119 is what that costs.
        if (auto * const document = qobject_cast<QmlJSEditorDocument *>(editor->document())) {
            auto * const outline = new QmlJSOutline(editor, document);
            outline->setParent(editor);
            QObject::connect(editor, &Core::IEditor::cursorPositionChanged, outline,
                             [outline] { outline->updateIndex(); });
            QObject::connect(document, &QmlJSEditorDocument::semanticInfoUpdated, outline,
                             [outline] { outline->updateIndex(); });
        }
        auto * const findReferences = new FindReferences(editor);
        QObject::connect(requests, &TextEditor::SymbolRequests::requestUsages, editor,
                [editor, findReferences](const QTextCursor &cursor) {
                    findQmlJSUsages(qobject_cast<TextEditor::TextDocument *>(editor->document()),
                                    cursor, findReferences);
                });
        QObject::connect(requests, &TextEditor::SymbolRequests::requestRename, editor,
                [editor, findReferences](const QTextCursor &cursor) {
                    renameQmlJSSymbol(qobject_cast<TextEditor::TextDocument *>(editor->document()),
                                      cursor, findReferences);
                });
    });
    setAutoCompleterCreator([]() { return new AutoCompleter; });
    setCommentDefinition(Utils::CommentDefinition::CppStyle);
    setParenthesesMatchingEnabled(true);
    setCodeFoldingSupported(true);

    addHoverHandler(&qmlJSHoverHandler());
    addHoverHandler(&colorPreviewHoverHandler());
    setLinkFinder(&findQmlJSLinkAt);
    addHoverHandler(&ProjectExplorer::resourcePreviewHoverHandler());

    setCompletionAssistProvider(new QmlJSCompletionAssistProvider);

    // The QML entries a right click offers. QmlJSEditorWidget names this
    // container in its own contextMenuEvent(), which a view that is not a
    // widget cannot do - so a QML file in the Qt Quick view got the plain text
    // menu and nothing of its language's.
    setContextMenuId(Constants::M_CONTEXT);

    // Both laying-out commands up front; followTheFormatter() narrows to the
    // one the code style calls for once there is an editor to narrow.
    setOptionalActionMask(OptionalActions::Format | qmlJSFixedOptionalActions);
}

static void decorateDocument(TextEditor::TextDocument *document)
{
    document->resetSyntaxHighlighter([] { return new QmlJSHighlighter(); });
    document->setIndenter(createQmlJsIndenter(document->document()));
}

namespace Internal {

void inspectElement()
{
    if (Core::IEditor * const editor = EditorManager::currentEditor())
        inspectElementIn(editor);
}

void showContextPane()
{
    if (auto editor = qobject_cast<QmlJSEditorWidget*>(EditorManager::currentEditor()->widget()))
        editor->showContextPane();
}

void setupQmlJSEditor()
{
    static QmlJSEditorFactory theQmlJSEditorFactory;

    TextEditor::SnippetProvider::registerGroup(Constants::QML_SNIPPETS_GROUP_ID,
                                               Tr::tr("QML", "SnippetProvider"),
                                               &decorateDocument,
                                               "text/x-qml",
                                               [] { return new AutoCompleter; });

}

} // namespace Internal

QdsSettings::QdsSettings()
{
    connect(&settings().qdsCommand, &FilePathAspect::changed, this, &QdsSettings::changed);
}

void QdsSettings::setQdsSettingVisible(bool visible)
{
    Internal::settings().qdsCommand.setVisible(visible);
}

FilePath QdsSettings::qdsCommand()
{
    const FilePath command = Internal::settings().qdsCommand.effectiveBinary();
    if (command.isEmpty())
        return Internal::settings().defaultQdsCommand();
    return command;
}

QdsSettings &qdsSettings()
{
    static QdsSettings settings;
    return settings;
}

// namespace Internal

} // namespace QmlJSEditor

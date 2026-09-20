// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cmakeoutline.h"

#include "cmakeeditor.h"

#include <cmakelang/cmakeastvisitor.h>
#include <cmakelang/cmakedocument.h>

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/find/itemviewfind.h>

#include <projectexplorer/projectexplorericons.h>

#include <texteditor/ioutlinewidget.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/navigationtreeview.h>
#include <utils/plaintextedit/plaintextedit.h>
#include <utils/qtcassert.h>
#include <utils/treeviewcombobox.h>
#include <utils/utilsicons.h>

#include <QSortFilterProxyModel>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <utils/temporarydirectory.h>

#include <QScopeGuard>
#include <QTest>
#include <QTreeView>
#endif

using namespace CMakeLang;
using namespace TextEditor;
using namespace Utils;
using namespace Utils::Text;
using namespace std::chrono_literals;

namespace CMakeProjectManager::Internal {

enum ItemKind { ProjectItem, FunctionItem, MacroItem, TargetItem, VariableItem };

static QIcon iconFor(ItemKind kind)
{
    static const QIcon icons[] = {CodeModelIcon::iconForType(CodeModelIcon::Namespace),
                                  CodeModelIcon::iconForType(CodeModelIcon::FuncPublic),
                                  CodeModelIcon::iconForType(CodeModelIcon::Macro),
                                  ProjectExplorer::Icons::BUILD_SMALL.icon(),
                                  CodeModelIcon::iconForType(CodeModelIcon::VarPublic)};
    return icons[kind];
}

static QString displayNameFor(ItemKind kind, const QString &name)
{
    switch (kind) {
    case ProjectItem:
        return QString("project(%1)").arg(name);
    case FunctionItem:
    case MacroItem:
        return name + "()";
    case TargetItem:
    case VariableItem:
        break;
    }
    return name;
}

class OutlineItem final : public TreeItem
{
public:
    OutlineItem(ItemKind kind, const QString &name, const Range &range)
        : kind(kind)
        , name(name)
        , range(range)
    {}

    QVariant data(int /*column*/, int role) const final
    {
        switch (role) {
        case Qt::DisplayRole:
            return displayNameFor(kind, name);
        case Qt::DecorationRole:
            return iconFor(kind);
        default:
            return {};
        }
    }

    const ItemKind kind;
    const QString name;
    const Range range;
};

// Turns the AST into the items, a function or macro taking what it names in
// its body as its children.
class OutlineBuilder final : public Visitor
{
public:
    explicit OutlineBuilder(TreeItem *root) { m_parents.append(root); }

private:
    bool visit(FunctionAST *ast) final { return open(FunctionItem, ast); }
    void endVisit(FunctionAST *) final { m_parents.removeLast(); }
    bool visit(MacroAST *ast) final { return open(MacroItem, ast); }
    void endVisit(MacroAST *) final { m_parents.removeLast(); }
    bool visit(CommandAST *ast) final;

    bool open(ItemKind kind, NestedCommandAST *ast);
    OutlineItem *append(ItemKind kind, CommandAST *command, const Position &end);

    QList<TreeItem *> m_parents;
};

bool OutlineBuilder::visit(CommandAST *ast)
{
    static const QHash<QString, ItemKind> kinds{{"project", ProjectItem},
                                                {"add_executable", TargetItem},
                                                {"add_library", TargetItem},
                                                {"add_custom_target", TargetItem},
                                                {"qt_add_executable", TargetItem},
                                                {"qt6_add_executable", TargetItem},
                                                {"qt_add_library", TargetItem},
                                                {"qt6_add_library", TargetItem},
                                                {"qt_add_plugin", TargetItem},
                                                {"qt6_add_plugin", TargetItem},
                                                {"qt_add_qml_module", TargetItem},
                                                {"qt6_add_qml_module", TargetItem},
                                                {"set", VariableItem},
                                                {"option", VariableItem}};

    const auto it = kinds.constFind(ast->commandName().toLower());
    if (it != kinds.constEnd())
        append(*it, ast, {});
    return false;
}

bool OutlineBuilder::open(ItemKind kind, NestedCommandAST *ast)
{
    Position end;
    if (CommandAST *closeCommand = ast->closeCommand)
        end = {closeCommand->rightParen.line, closeCommand->rightParen.column};

    OutlineItem *item = append(kind, ast->openCommand, end);
    m_parents.append(item ? item : m_parents.last());
    return true;
}

OutlineItem *OutlineBuilder::append(ItemKind kind, CommandAST *command, const Position &end)
{
    ArgumentAST *argument = command->arguments().first();
    if (!argument)
        return nullptr;

    const QString name = argument->value();
    if (name.isEmpty() || name.contains("${"))
        return nullptr;

    TreeItem *parent = m_parents.last();
    if (kind == VariableItem) {
        for (TreeItem *sibling : *parent) {
            const auto item = static_cast<OutlineItem *>(sibling);
            if (item->kind == VariableItem && item->name == name)
                return nullptr;
        }
    }

    const Position begin{argument->token.line, argument->token.column - 1};
    auto item = new OutlineItem(kind, name, {begin, end.isValid() ? end : begin});
    parent->appendChild(item);
    return item;
}

CMakeOutlineModel::CMakeOutlineModel(TextDocument *document)
    : TreeModel(document)
    , m_document(document)
{
    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(500ms);
    connect(&m_updateTimer, &QTimer::timeout, this, &CMakeOutlineModel::rebuild);
    connect(document->document(), &QTextDocument::contentsChanged, this, [this] {
        m_updateTimer.start();
    });
}

const OutlineItem *CMakeOutlineModel::itemAt(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;

    TreeItem *item = itemForIndex(index);
    if (!item || item == rootItem())
        return nullptr;
    return static_cast<OutlineItem *>(item);
}

Position CMakeOutlineModel::positionFromIndex(const QModelIndex &index) const
{
    const OutlineItem *item = itemAt(index);
    return item ? item->range.begin : Position();
}

bool CMakeOutlineModel::isVariable(const QModelIndex &index) const
{
    const OutlineItem *item = itemAt(index);
    return item && item->kind == VariableItem;
}

QModelIndex CMakeOutlineModel::indexForPosition(const Position &position,
                                                Match match,
                                                const QModelIndex &parent) const
{
    QModelIndex result = parent;
    for (int row = 0, rows = rowCount(parent); row < rows; ++row) {
        const QModelIndex candidate = index(row, 0, parent);
        const OutlineItem *item = itemAt(candidate);
        if (!item)
            continue;
        if (item->range.begin.line > position.line)
            break;
        if (match == MatchWithoutVariables && item->kind == VariableItem)
            continue;
        // What has a body of its own only counts when the position is in it.
        if (item->range.end != item->range.begin && !item->range.contains(position))
            continue;
        result = candidate;
    }

    if (result == parent)
        return result;
    return indexForPosition(position, match, result);
}

void CMakeOutlineModel::rebuild()
{
    const DocumentPtr document = Document::fromSource(m_document->plainText());
    if (!document->isValid())
        return;

    auto root = new TreeItem;
    OutlineBuilder builder(root);
    builder.accept(document->ast());
    setRootItem(root);
}

//
// CMakeOutlineWidget
//

// The editor, not the widget: what the pane needs from the view is where the
// caret is and how to move it, and both views answer that.
class CMakeOutlineWidget final : public IOutlineWidget
{
public:
    CMakeOutlineWidget(Core::IEditor *editor, CMakeOutlineModel *model);

private:
    QList<QAction *> filterMenuActions() const final { return {}; }
    void setCursorSynchronization(bool syncWithCursor) final;
    bool isSorted() const final { return m_sorted; }
    void setSorted(bool sorted) final;
    void restoreSettings(const QVariantMap &map) final;
    QVariantMap settings() const final;

    void updateIndex();
    void gotoItem(const QModelIndex &proxyIndex);

    Core::IEditor * const m_editor;
    CMakeOutlineModel * const m_model;
    QSortFilterProxyModel * const m_proxyModel;
    NavigationTreeView * const m_treeView;
    bool m_syncWithCursor = true;
    bool m_blockCursorSync = false;
    bool m_sorted = false;
};

CMakeOutlineWidget::CMakeOutlineWidget(Core::IEditor *editor, CMakeOutlineModel *model)
    : m_editor(editor)
    , m_model(model)
    , m_proxyModel(new QSortFilterProxyModel(this))
    , m_treeView(new NavigationTreeView(this))
{
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setDynamicSortFilter(true);
    m_proxyModel->sort(-1);

    m_treeView->setModel(m_proxyModel);
    m_treeView->setExpandsOnDoubleClick(false);
    m_treeView->expandAll();
    setFocusProxy(m_treeView);

    auto layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(Core::ItemViewFind::createSearchableWrapper(m_treeView));
    setLayout(layout);

    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        m_treeView->expandAll();
        updateIndex();
    });
    connect(m_treeView, &QAbstractItemView::activated, this, &CMakeOutlineWidget::gotoItem);
    connect(editor, &Core::IEditor::cursorPositionChanged, this, &CMakeOutlineWidget::updateIndex);

    updateIndex();
}

void CMakeOutlineWidget::setCursorSynchronization(bool syncWithCursor)
{
    m_syncWithCursor = syncWithCursor;
    updateIndex();
}

void CMakeOutlineWidget::setSorted(bool sorted)
{
    m_sorted = sorted;
    m_proxyModel->sort(m_sorted ? 0 : -1);
}

void CMakeOutlineWidget::restoreSettings(const QVariantMap &map)
{
    setSorted(map.value(QString("CMakeOutline.Sort"), false).toBool());
}

QVariantMap CMakeOutlineWidget::settings() const
{
    return {{QString("CMakeOutline.Sort"), m_sorted}};
}

void CMakeOutlineWidget::updateIndex()
{
    if (!m_syncWithCursor || m_blockCursorSync)
        return;

    const QModelIndex index = m_model->indexForPosition(TextEditor::lineColumnOf(m_editor));
    if (!index.isValid())
        return;

    const QModelIndex proxyIndex = m_proxyModel->mapFromSource(index);
    m_blockCursorSync = true;
    m_treeView->setCurrentIndex(proxyIndex);
    m_treeView->scrollTo(proxyIndex);
    m_blockCursorSync = false;
}

void CMakeOutlineWidget::gotoItem(const QModelIndex &proxyIndex)
{
    const Position position = m_model->positionFromIndex(m_proxyModel->mapToSource(proxyIndex));
    if (!position.isValid())
        return;

    m_blockCursorSync = true;
    Core::EditorManager::cutForwardNavigationHistory();
    Core::EditorManager::addCurrentPositionToNavigationHistory();
    m_editor->gotoLine(position.line, position.column, true);
    m_blockCursorSync = false;

    TextEditor::setFocusIn(m_editor);
}

//
// The combo box of the editor tool bar
//

// The combo box names what the file defines. The variables it sets are left to
// the outline pane: there are too many of them to pick one from a drop down.
class OutlineComboFilterModel final : public QSortFilterProxyModel
{
public:
    OutlineComboFilterModel(CMakeOutlineModel *sourceModel, QObject *parent)
        : QSortFilterProxyModel(parent)
        , m_sourceModel(sourceModel)
    {
        setSourceModel(sourceModel);
    }

private:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const final
    {
        return !m_sourceModel->isVariable(m_sourceModel->index(sourceRow, 0, sourceParent));
    }

    CMakeOutlineModel * const m_sourceModel;
};

QWidget *createCMakeOutlineComboBox(TextEditorWidget *editorWidget, CMakeOutlineModel *model)
{
    auto combo = new TreeViewComboBox;
    auto proxyModel = new OutlineComboFilterModel(model, combo);
    combo->setModel(proxyModel);
    combo->setMinimumContentsLength(13);
    combo->setMaxVisibleItems(40);

    QSizePolicy policy = combo->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    combo->setSizePolicy(policy);

    const auto updateIndex = [combo, proxyModel, model, editorWidget] {
        const QModelIndex index = model->indexForPosition(editorWidget->lineColumn(),
                                                          CMakeOutlineModel::MatchWithoutVariables);
        if (!index.isValid())
            return;

        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(proxyModel->mapFromSource(index));
        combo->setToolTip(combo->currentText());
    };

    QObject::connect(model, &QAbstractItemModel::modelReset, combo, [combo, updateIndex] {
        combo->view()->expandAll();
        updateIndex();
    });
    QObject::connect(editorWidget,
                     &PlainTextEdit::cursorPositionChanged,
                     combo,
                     updateIndex);
    QObject::connect(combo, &QComboBox::activated, combo, [combo, proxyModel, model, editorWidget] {
        const QModelIndex index = proxyModel->mapToSource(combo->view()->currentIndex());
        const Position position = model->positionFromIndex(index);
        if (!position.isValid())
            return;

        Core::EditorManager::cutForwardNavigationHistory();
        Core::EditorManager::addCurrentPositionToNavigationHistory();
        editorWidget->gotoLine(position.line, position.column, true, true);
        emit editorWidget->activateEditor();
    });

    combo->view()->expandAll();
    updateIndex();
    return combo;
}

// The same for a view that draws its own tool bar: which command the caret is
// in, from the same model, with the variables left out the way the combo
// leaves them out. Parented to the editor, which is where the view finds it.
class CMakeToolBarOutline final : public ToolBarOutline
{
public:
    CMakeToolBarOutline(Core::IEditor *editor, CMakeOutlineModel *model)
        : ToolBarOutline(editor)
        , m_editor(editor)
        , m_model(model)
        , m_proxyModel(new OutlineComboFilterModel(model, this))
    {
        connect(editor, &Core::IEditor::cursorPositionChanged, this,
                [this] { updateIndex(false); });
        // A rebuilt model has new indexes for the same rows.
        connect(model, &QAbstractItemModel::modelReset, this, [this] { updateIndex(true); });
        updateIndex(true);
    }

    QAbstractItemModel *model() const final { return m_proxyModel; }
    QModelIndex currentIndex() const final { return m_current; }

    QString currentText() const final
    {
        return m_current.isValid() ? m_current.data().toString() : QString();
    }

    void activate(const QModelIndex &index) final
    {
        const Position position = m_model->positionFromIndex(m_proxyModel->mapToSource(index));
        if (!position.isValid())
            return;

        Core::EditorManager::cutForwardNavigationHistory();
        Core::EditorManager::addCurrentPositionToNavigationHistory();
        m_editor->gotoLine(position.line, position.column, true);
        TextEditor::setFocusIn(m_editor);
    }

private:
    void updateIndex(bool force)
    {
        const QModelIndex index = m_proxyModel->mapFromSource(
            m_model->indexForPosition(TextEditor::lineColumnOf(m_editor),
                                      CMakeOutlineModel::MatchWithoutVariables));
        if (!force && index == m_current)
            return;
        m_current = index;
        emit currentIndexChanged();
    }

    Core::IEditor * const m_editor;
    CMakeOutlineModel * const m_model;
    OutlineComboFilterModel * const m_proxyModel;
    QModelIndex m_current;
};

ToolBarOutline *createCMakeToolBarOutline(Core::IEditor *editor, CMakeOutlineModel *model)
{
    return new CMakeToolBarOutline(editor, model);
}

//
// CMakeOutlineWidgetFactory
//

class CMakeOutlineWidgetFactory final : public IOutlineWidgetFactory
{
public:
    bool supportsEditor(Core::IEditor *editor) const final
    {
        return qobject_cast<CMakeTextDocument *>(editor->document()) != nullptr;
    }

    bool supportsSorting() const final { return true; }

    IOutlineWidget *createWidget(Core::IEditor *editor) final
    {
        const auto document = qobject_cast<CMakeTextDocument *>(editor->document());
        QTC_ASSERT(document, return nullptr);
        return new CMakeOutlineWidget(editor, document->outlineModel());
    }
};

void setupCMakeOutline()
{
    static CMakeOutlineWidgetFactory theCMakeOutlineWidgetFactory;
}

#ifdef WITH_TESTS

// The outline pane follows the caret and moves it, and the tool bar row says
// which command the caret is in; both are things any view of a document can
// do. Both asked for the widget editor, so a CMakeLists.txt in the Qt Quick
// view got no outline pane - createWidget() soft-asserted instead - and no
// outline in its tool bar.
class CMakeOutlineTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheOutlineFollowsTheCaretInAnyView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testTheOutlineFollowsTheCaretInAnyView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("cmake-outline");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("CMakeLists.txt");
        QVERIFY(file.writeFileContents("cmake_minimum_required(VERSION 3.16)\n"
                                       "project(Outline)\n"
                                       "set(SOURCES main.cpp)\n"
                                       "function(helper)\n"
                                       "endfunction()\n"
                                       "add_executable(app ${SOURCES})\n"));

        TextEditorFactory * const editorFactory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a CMakeLists.txt");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
        auto * const document = qobject_cast<CMakeTextDocument *>(editor->document());
        QVERIFY(document);

        editor->widget()->resize(400, 300);
        editor->widget()->show();
        const QScopeGuard hideIt([editor] { editor->widget()->hide(); });

        // A row by what it says, wherever it is nested: which row it is is not
        // what this is about.
        const auto rowNamed = [](const QAbstractItemModel *model, const QString &name) {
            const auto walk = [model, &name](const QModelIndex &parent,
                                             const auto &self) -> QModelIndex {
                for (int row = 0; row < model->rowCount(parent); ++row) {
                    const QModelIndex candidate = model->index(row, 0, parent);
                    if (candidate.data().toString() == name)
                        return candidate;
                    if (const QModelIndex inner = self(candidate, self); inner.isValid())
                        return inner;
                }
                return QModelIndex();
            };
            return walk(QModelIndex(), walk);
        };
        const QString text = document->plainText();
        const auto caretAt = [editor, document, &text](const char *needle) {
            const int offset = text.indexOf(QLatin1String(needle));
            const Utils::Text::Position at
                = Utils::Text::Position::fromPositionInDocument(document->document(), offset);
            editor->gotoLine(at.line, at.column);
        };

        // The pane.
        CMakeOutlineWidgetFactory factory;
        QVERIFY2(factory.supportsEditor(editor),
                 "the outline does not offer itself for a CMakeLists.txt in this view");
        const std::unique_ptr<IOutlineWidget> outline(factory.createWidget(editor));
        QVERIFY2(outline.get(), "the outline built nothing");
        outline->setCursorSynchronization(true);

        auto * const tree = outline->findChild<QTreeView *>();
        QVERIFY(tree && tree->model());
        QTRY_VERIFY2(tree->model()->rowCount() > 0,
                     "the outline never listed anything in the file");
        QVERIFY2(rowNamed(tree->model(), "SOURCES").isValid(),
                 "the pane does not list the variable");

        // Into the function, and the pane has to say so; then out again, so
        // that what is asserted is the pane following rather than one row
        // happening to be current.
        caretAt("helper");
        QTRY_COMPARE(tree->currentIndex().data().toString(), QString("helper()"));
        caretAt("app ");
        QTRY_COMPARE(tree->currentIndex().data().toString(), QString("app"));

        // The other direction: choosing a row moves the caret. What a double
        // click or Return sends.
        const QModelIndex helperRow = rowNamed(tree->model(), "helper()");
        QVERIFY2(helperRow.isValid(), "the pane lists no helper()");
        QVERIFY2(editor->currentLine() != 4,
                 "the caret was already there, so nothing below asserts anything");
        emit tree->activated(helperRow);
        QTRY_COMPARE(editor->currentLine(), 4);

        // The tool bar row's outline. The widget editor fills a combo of its
        // own; a view that is not one is handed a ToolBarOutline and draws it.
        auto * const toolBarOutline = editor->findChild<ToolBarOutline *>();
        QCOMPARE(toolBarOutline != nullptr, quick);
        if (!quick)
            return;

        QTRY_VERIFY2(toolBarOutline->model()->rowCount() > 0, "the tool bar outline lists nothing");
        QVERIFY2(rowNamed(toolBarOutline->model(), "app").isValid(),
                 "the tool bar outline does not list the target");
        QVERIFY2(!rowNamed(toolBarOutline->model(), "SOURCES").isValid(),
                 "the tool bar outline lists a variable, which is the pane's to list");

        caretAt("helper");
        QTRY_COMPARE(toolBarOutline->currentText(), QString("helper()"));
        caretAt("project(");
        QTRY_COMPARE(toolBarOutline->currentText(), QString("project(Outline)"));

        const QModelIndex appRow = rowNamed(toolBarOutline->model(), "app");
        QVERIFY(appRow.isValid());
        toolBarOutline->activate(appRow);
        QTRY_COMPARE(editor->currentLine(), 6);
    }
};

QObject *createCMakeOutlineTest()
{
    return new CMakeOutlineTest;
}

#endif // WITH_TESTS

} // CMakeProjectManager::Internal

#ifdef WITH_TESTS
#include "cmakeoutline.moc"
#endif

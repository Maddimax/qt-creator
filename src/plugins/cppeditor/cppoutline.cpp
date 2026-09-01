// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppoutline.h"

#include "cppeditordocument.h"
#include "cppeditoroutline.h"
#include "cppeditortr.h"
#include "clangdsettings.h"
#include "cppmodelmanager.h"
#include "cppoutlinemodel.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/find/itemviewfind.h>

#include <texteditor/ioutlinewidget.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/navigationtreeview.h>
#include <utils/qtcassert.h>

#include <QMenu>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <coreplugin/editormanager/ieditor.h>
#include <utils/temporarydirectory.h>
#include <QScopeGuard>
#include <QTest>
#include <QTreeView>
#endif

using namespace TextEditor;

namespace CppEditor::Internal {

class CppOutlineTreeView final : public Utils::NavigationTreeView
{
public:
    CppOutlineTreeView(QWidget *parent) :
        Utils::NavigationTreeView(parent)
    {
        setExpandsOnDoubleClick(false);
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
    }

    void contextMenuEvent(QContextMenuEvent *event) final
    {
        if (!event)
            return;

        QMenu contextMenu;

        QAction *action = contextMenu.addAction(Tr::tr("Expand All"));
        connect(action, &QAction::triggered, this, &QTreeView::expandAll);
        action = contextMenu.addAction(Tr::tr("Collapse All"));
        connect(action, &QAction::triggered, this, &QTreeView::collapseAll);

        contextMenu.exec(event->globalPos());

        event->accept();
    }
};

class CppOutlineFilterModel : public QSortFilterProxyModel
{
public:
    CppOutlineFilterModel(OutlineModel &sourceModel, QObject *parent)
        : QSortFilterProxyModel(parent)
        , m_sourceModel(sourceModel)
    {}

    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const final
    {
        // ignore artificial "<Select Symbol>" entry
        if (!sourceParent.isValid() && sourceRow == 0)
            return false;
        // ignore generated symbols, e.g. by macro expansion (Q_OBJECT)
        const QModelIndex sourceIndex = m_sourceModel.index(sourceRow, 0, sourceParent);
        if (m_sourceModel.isGenerated(sourceIndex))
            return false;

        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }

    Qt::DropActions supportedDragActions() const final
    {
        return sourceModel()->supportedDragActions();
    }

private:
    OutlineModel &m_sourceModel;
};

class CppOutlineWidget : public TextEditor::IOutlineWidget
{
public:
    CppOutlineWidget(Core::IEditor *editor, CppEditorDocument *document);

    // IOutlineWidget
    QList<QAction*> filterMenuActions() const final;
    void setCursorSynchronization(bool syncWithCursor) final;
    bool isSorted() const final;
    void setSorted(bool sorted) final;

    void restoreSettings(const QVariantMap &map) final;
    QVariantMap settings() const final;

private:
    void modelUpdated();
    void updateIndex();
    void updateIndexNow();
    void updateTextCursor(const QModelIndex &index);
    void onItemActivated(const QModelIndex &index);
    bool syncCursor();

    // The editor, not the widget: what this needs from the view is where the
    // caret is and how to move it, and both views answer that.
    Core::IEditor * const m_editor;
    CppEditorDocument * const m_document;
    CppOutlineTreeView *m_treeView;
    OutlineModel * const m_model;
    QSortFilterProxyModel *m_proxyModel;
    QTimer m_updateIndexTimer;

    bool m_enableCursorSync;
    bool m_blockCursorSync;
    bool m_sorted;
};

CppOutlineWidget::CppOutlineWidget(Core::IEditor *editor, CppEditorDocument *document) :
    m_editor(editor),
    m_document(document),
    m_treeView(new CppOutlineTreeView(this)),
    m_model(&m_document->outlineModel()),
    m_proxyModel(new CppOutlineFilterModel(*m_model, this)),
    m_enableCursorSync(true),
    m_blockCursorSync(false),
    m_sorted(false)
{
    m_proxyModel->setSourceModel(m_model);

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(Core::ItemViewFind::createSearchableWrapper(m_treeView));
    setLayout(layout);

    m_treeView->setModel(m_proxyModel);
    m_treeView->setSortingEnabled(true);
    setFocusProxy(m_treeView);

    connect(m_model, &QAbstractItemModel::modelReset, this, &CppOutlineWidget::modelUpdated);
    modelUpdated();

    connect(m_treeView, &QAbstractItemView::activated,
            this, &CppOutlineWidget::onItemActivated);
    connect(editor, &Core::IEditor::cursorPositionChanged, this, [this] {
        if (m_model->rootItem()->hasChildren())
            updateIndex();
    });

    m_updateIndexTimer.setSingleShot(true);
    m_updateIndexTimer.setInterval(500);
    connect(&m_updateIndexTimer, &QTimer::timeout, this, &CppOutlineWidget::updateIndexNow);
}

QList<QAction*> CppOutlineWidget::filterMenuActions() const
{
    return {};
}

void CppOutlineWidget::setCursorSynchronization(bool syncWithCursor)
{
    m_enableCursorSync = syncWithCursor;
    if (m_enableCursorSync)
        updateIndexNow();
}

bool CppOutlineWidget::isSorted() const
{
    return m_sorted;
}

void CppOutlineWidget::setSorted(bool sorted)
{
    m_sorted = sorted;
    m_proxyModel->sort(m_sorted ? 0 : -1);
}

void CppOutlineWidget::restoreSettings(const QVariantMap &map)
{
    setSorted(map.value(QString("CppOutline.Sort"), false).toBool());
}

QVariantMap CppOutlineWidget::settings() const
{
    return {{QString("CppOutline.Sort"), m_sorted}};
}

void CppOutlineWidget::modelUpdated()
{
    m_treeView->expandAll();
}

void CppOutlineWidget::updateIndex()
{
    m_updateIndexTimer.start();
}

void CppOutlineWidget::updateIndexNow()
{
    if (!syncCursor())
        return;

    const int revision = m_document->document()->revision();
    if (m_model->editorRevision() != revision) {
        m_document->updateOutline();
        return;
    }

    m_updateIndexTimer.stop();

    // IEditor counts both from one; Text::Position counts the column from zero.
    const Utils::Text::Position caret{m_editor->currentLine(), m_editor->currentColumn() - 1};
    if (QModelIndex index = m_model->indexForPosition(caret); index.isValid()) {
        m_blockCursorSync = true;
        QModelIndex proxyIndex = m_proxyModel->mapFromSource(index);
        m_treeView->setCurrentIndex(proxyIndex);
        m_treeView->scrollTo(proxyIndex);
        m_blockCursorSync = false;
    }
}

void CppOutlineWidget::updateTextCursor(const QModelIndex &proxyIndex)
{
    QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
    Utils::Text::Position lineColumn = m_document->outlineModel().positionFromIndex(index);
    if (!lineColumn.isValid())
        return;

    m_blockCursorSync = true;

    Core::EditorManager::cutForwardNavigationHistory();
    Core::EditorManager::addCurrentPositionToNavigationHistory();

    m_editor->gotoLine(lineColumn.line, lineColumn.column, true);
    m_blockCursorSync = false;
}

void CppOutlineWidget::onItemActivated(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    updateTextCursor(index);
    if (QWidget * const view = m_editor->widget())
        view->setFocus();
}

bool CppOutlineWidget::syncCursor()
{
    return m_enableCursorSync && !m_blockCursorSync;
}

class CppOutlineWidgetFactory final : public IOutlineWidgetFactory
{
public:
    bool supportsEditor(Core::IEditor *editor) const final
    {
        // The context says it is a C++ editor; which view it is does not.
        const auto document = qobject_cast<CppEditorDocument *>(editor->document());
        if (!document || !CppModelManager::isCppEditor(editor))
            return false;
        return !CppModelManager::usesClangd(document);
    }

    bool supportsSorting() const final
    {
        return true;
    }

    IOutlineWidget *createWidget(Core::IEditor *editor) final
    {
        const auto document = qobject_cast<CppEditorDocument *>(editor->document());
        QTC_ASSERT(document, return nullptr);
        return new CppOutlineWidget(editor, document);
    }
};

void setupCppOutline()
{
    static CppOutlineWidgetFactory theCppOutlineWidgetFactory;
}

#ifdef WITH_TESTS

// The outline follows the caret and moves it, and both are things any view of
// a document can do. It asked for a CppEditorWidget, so a C++ file in the Qt
// Quick editor got no outline at all.
class CppOutlineTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheOutlineFollowsTheCaretInAnyView()
    {
        Utils::TemporaryDirectory dir("cpp-outline-without-a-widget");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents("void alpha() {}\n\nvoid beta()\n{\n}\n"));

        // The built-in code model, so that this outline is the one on offer.
        // clangd brings the language client's, which is a different factory.
        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a C++ file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        CppOutlineWidgetFactory factory;
        QVERIFY2(factory.supportsEditor(editor),
                 "the outline does not offer itself for a C++ file in the Quick editor");

        const std::unique_ptr<TextEditor::IOutlineWidget> outline(factory.createWidget(editor));
        QVERIFY2(outline.get(), "the outline built nothing");
        outline->setCursorSynchronization(true);

        auto * const tree = outline->findChild<QTreeView *>();
        QVERIFY(tree && tree->model());
        QTRY_VERIFY2(tree->model()->rowCount() >= 2,
                     "the outline never listed the two functions in the file");

        // Into beta(), and the outline has to say so. gotoLine is 1-based.
        editor->gotoLine(3, 6);
        QTRY_COMPARE(tree->currentIndex().data().toString(), QString("beta(): void"));

        // And back, so that what is asserted is the outline following rather
        // than one row happening to be current.
        editor->gotoLine(1, 6);
        QTRY_COMPARE(tree->currentIndex().data().toString(), QString("alpha(): void"));

        // The other direction: picking a row moves the caret, which is the
        // half of the outline a reader actually uses.
        // Found by what it says rather than by its number: the view sorts, and
        // which row beta() is in is not what this is about.
        QModelIndex betaRow;
        for (int row = 0; row < tree->model()->rowCount(); ++row) {
            const QModelIndex candidate = tree->model()->index(row, 0);
            if (candidate.data().toString() == "beta(): void")
                betaRow = candidate;
        }
        QVERIFY2(betaRow.isValid(), "the outline lists no beta()");
        QMetaObject::invokeMethod(tree, "activated", Q_ARG(QModelIndex, betaRow));
        QTRY_COMPARE(editor->currentLine(), 3);
    }
};

QObject *createCppOutlineTest()
{
    return new CppOutlineTest;
}

#endif // WITH_TESTS

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include "cppoutline.moc"
#endif

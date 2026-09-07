// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmljseditordocument.h"
#include "qmljseditortr.h"
#include "qmljsoutline.h"
#include "qmljsoutlinetreeview.h"
#include "qmloutlinemodel.h"

#include <coreplugin/find/itemviewfind.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>

#include <texteditor/ioutlinewidget.h>
#include <texteditor/texteditor.h>

#include <utils/qtcassert.h>
#include <utils/textutils.h>

#include <QAction>
#include <QSortFilterProxyModel>
#include <QTextBlock>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <coreplugin/editormanager/editormanager.h>
#include <utils/temporarydirectory.h>
#include <QScopeGuard>
#include <QTest>
#include <QTreeView>
#endif

using namespace QmlJS;

enum {
    debug = false
};

namespace QmlJSEditor::Internal {

// QmlJSOutlineFilterModel

class QmlJSOutlineFilterModel final : public QSortFilterProxyModel
{
public:
    QmlJSOutlineFilterModel()
    {
        setDynamicSortFilter(true);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const final;
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const final;
    bool lessThan(const QModelIndex &sourceLeft, const QModelIndex &sourceRight) const final;
    QVariant data(const QModelIndex &index, int role) const final;
    Qt::DropActions supportedDragActions() const final;

    bool filterBindings() const;
    void setFilterBindings(bool filterBindings);
    void setSorted(bool sorted);

private:
    bool m_filterBindings = false;
    bool m_sorted = false;
};

Qt::ItemFlags QmlJSOutlineFilterModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = sourceModel()->flags(index);
    if (m_sorted)
        f.setFlag(Qt::ItemIsDropEnabled, false);
    return f;
}

bool QmlJSOutlineFilterModel::filterAcceptsRow(int sourceRow,
                                               const QModelIndex &sourceParent) const
{
    if (m_filterBindings) {
        QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
        while (sourceIndex.isValid()) {
            if (sourceIndex.data(QmlOutlineModel::ItemTypeRole)
                == QmlOutlineModel::NonElementBindingType) {
                return false;
            }
            sourceIndex = sourceIndex.parent();
        }
    }
    return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

bool QmlJSOutlineFilterModel::lessThan(const QModelIndex &sourceLeft,
                                       const QModelIndex &sourceRight) const
{
    if (!m_sorted)
        return sourceLeft.row() > sourceRight.row();

    return sourceLeft.data().toString() > sourceRight.data().toString();
}

QVariant QmlJSOutlineFilterModel::data(const QModelIndex &index, int role) const
{
    if (role == QmlOutlineModel::AnnotationRole) {
        // Don't show element id etc behind element if the property is also visible
        if (!filterBindings()
                && index.data(QmlOutlineModel::ItemTypeRole) == QmlOutlineModel::ElementType) {
            return QVariant();
        }
    }
    return QSortFilterProxyModel::data(index, role);
}

Qt::DropActions QmlJSOutlineFilterModel::supportedDragActions() const
{
    return sourceModel()->supportedDragActions();
}

bool QmlJSOutlineFilterModel::filterBindings() const
{
    return m_filterBindings;
}

void QmlJSOutlineFilterModel::setFilterBindings(bool filterBindings)
{
    m_filterBindings = filterBindings;
    invalidateFilter();
}

void QmlJSOutlineFilterModel::setSorted(bool sorted)
{
    m_sorted = sorted;
    invalidate();
}

// QmlJSOutlineWidget

class QmlJSOutlineWidget final : public TextEditor::IOutlineWidget
{
public:
    QmlJSOutlineWidget(Core::IEditor *editor, QmlJSEditorDocument *document);

    // IOutlineWidget
    QList<QAction*> filterMenuActions() const final;
    void setCursorSynchronization(bool syncWithCursor) final;
    bool isSorted() const final { return m_sorted; };
    void setSorted(bool sorted) final;
    void restoreSettings(const QVariantMap &map) final;
    QVariantMap settings() const final;

private:
    void updateSelectionInTree();
    void updateSelectionInText(const QItemSelection &selection);
    void updateTextCursor(const QModelIndex &index);
    void focusEditor();
    void setShowBindings(bool showBindings);
    bool syncCursor();
    bool editorHasFocus() const;

private:
    // The editor, not the widget: what this needs from the view is where the
    // caret is and how to move it, and both views answer that.
    Core::IEditor * const m_editor;
    QmlJSEditorDocument * const m_document;
    QmlJSOutlineTreeView *m_treeView = nullptr;
    QmlJSOutlineFilterModel m_filterModel;

    QAction *m_showBindingsAction = nullptr;

    bool m_enableCursorSync = true;
    bool m_blockCursorSync = false;
    bool m_sorted = false;
};

QmlJSOutlineWidget::QmlJSOutlineWidget(Core::IEditor *editor, QmlJSEditorDocument *document)
    : m_editor(editor)
    , m_document(document)
    , m_treeView(new QmlJSOutlineTreeView(this))
{
    m_filterModel.setFilterBindings(false);
    m_filterModel.setSourceModel(m_document->outlineModel());

    m_treeView->setModel(&m_filterModel);
    m_treeView->setSortingEnabled(true);
    m_treeView->expandAll();

    setFocusProxy(m_treeView);

    m_showBindingsAction = new QAction(this);
    m_showBindingsAction->setText(Tr::tr("Show All Bindings"));
    m_showBindingsAction->setCheckable(true);
    m_showBindingsAction->setChecked(true);
    connect(m_showBindingsAction, &QAction::toggled, this, &QmlJSOutlineWidget::setShowBindings);

    // A reset drops every index the selection model holds, and letting it
    // report that as the reader changing the selection would move the caret.
    connect(m_document->outlineModel(), &QAbstractItemModel::modelAboutToBeReset, m_treeView, [this] {
        if (m_treeView->selectionModel())
            m_treeView->selectionModel()->blockSignals(true);
    });
    connect(m_document->outlineModel(), &QAbstractItemModel::modelReset, m_treeView, [this] {
        if (m_treeView->selectionModel())
            m_treeView->selectionModel()->blockSignals(false);
    });

    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &QmlJSOutlineWidget::updateSelectionInText);
    connect(m_treeView, &QAbstractItemView::activated,
            this, &QmlJSOutlineWidget::focusEditor);

    // Where the caret is, asked of the editor rather than of a widget, and
    // again when the parse the model is built from is replaced.
    connect(m_editor, &Core::IEditor::cursorPositionChanged,
            this, &QmlJSOutlineWidget::updateSelectionInTree);
    connect(m_document->outlineModel(), &QmlOutlineModel::updated, this, [this] {
        m_treeView->expandAll();
        updateSelectionInTree();
    });

    auto layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(Core::ItemViewFind::createSearchableWrapper(m_treeView));
    setLayout(layout);

    updateSelectionInTree();
}

QList<QAction*> QmlJSOutlineWidget::filterMenuActions() const
{
    return {m_showBindingsAction};
}

void QmlJSOutlineWidget::setCursorSynchronization(bool syncWithCursor)
{
    m_enableCursorSync = syncWithCursor;
    updateSelectionInTree();
}

void QmlJSOutlineWidget::setSorted(bool sorted)
{
    m_sorted = sorted;
    m_filterModel.setSorted(m_sorted);
}

void QmlJSOutlineWidget::restoreSettings(const QVariantMap &map)
{
    bool showBindings = map.value(QString::fromLatin1("QmlJSOutline.ShowBindings"), true).toBool();
    m_showBindingsAction->setChecked(showBindings);
    setSorted(map.value(QString("QmlJSOutline.Sort"), false).toBool());
}

QVariantMap QmlJSOutlineWidget::settings() const
{
    return {
        {QString("QmlJSOutline.ShowBindings"), m_showBindingsAction->isChecked()},
        {QString("QmlJSOutline.Sort"), m_sorted}
    };
}

void QmlJSOutlineWidget::updateSelectionInTree()
{
    if (!syncCursor())
        return;
    const QmlJS::Document::Ptr parsed = m_document->outlineModel()->document();
    if (!parsed)
        return;
    // The model is built from a parse that is older than the text, so the
    // element it would name is the wrong one. QmlOutlineModel::updated brings
    // this back when the new parse lands.
    if (parsed->editorRevision() != m_document->document()->revision())
        return;

    m_blockCursorSync = true;

    const int caret = TextEditor::textCursorOf(m_editor).position();
    QModelIndex baseIndex = m_document->outlineModel()->indexForPosition(caret);
    QModelIndex filterIndex = m_filterModel.mapFromSource(baseIndex);
    while (baseIndex.isValid() && !filterIndex.isValid()) { // Search for ancestor index actually shown
        baseIndex = baseIndex.parent();
        filterIndex = m_filterModel.mapFromSource(baseIndex);
    }

    m_treeView->setCurrentIndex(filterIndex);
    m_treeView->scrollTo(filterIndex);
    m_blockCursorSync = false;
}

void QmlJSOutlineWidget::updateSelectionInText(const QItemSelection &selection)
{
    if (!syncCursor())
        return;

    if (!selection.indexes().isEmpty()) {
        QModelIndex index = selection.indexes().first();

        updateTextCursor(index);
    }
}

void QmlJSOutlineWidget::updateTextCursor(const QModelIndex &index)
{
    const auto update = [this](const QModelIndex &index) {
        // The reader is typing in the editor, so the tree is following the
        // caret rather than leading it.
        if (editorHasFocus())
            return;

        const QModelIndex sourceIndex = m_filterModel.mapToSource(index);
        const SourceLocation location
            = m_document->outlineModel()->sourceLocation(sourceIndex);
        if (!location.isValid())
            return;

        const QTextBlock lastBlock = m_document->document()->lastBlock();
        const uint textLength = lastBlock.position() + lastBlock.length();
        if (location.offset >= textLength)
            return;

        Core::EditorManager::cutForwardNavigationHistory();
        Core::EditorManager::addCurrentPositionToNavigationHistory();

        // IEditor counts the line from one and the column from zero.
        const Utils::Text::Position position
            = Utils::Text::Position::fromPositionInDocument(m_document->document(),
                                                            int(location.offset));
        m_editor->gotoLine(position.line, position.column, true);
    };
    m_blockCursorSync = true;
    update(index);
    m_blockCursorSync = false;
}

void QmlJSOutlineWidget::focusEditor()
{
    if (QWidget * const view = m_editor->widget())
        view->setFocus();
}

bool QmlJSOutlineWidget::editorHasFocus() const
{
    const QWidget * const view = m_editor->widget();
    return view && view->hasFocus();
}

void QmlJSOutlineWidget::setShowBindings(bool showBindings)
{
    m_filterModel.setFilterBindings(!showBindings);
    m_treeView->expandAll();
    updateSelectionInTree();
}

bool QmlJSOutlineWidget::syncCursor()
{
    return m_enableCursorSync && !m_blockCursorSync;
}

class QmlJSOutlineWidgetFactory final : public TextEditor::IOutlineWidgetFactory
{
public:
    bool supportsEditor(Core::IEditor *editor) const final
    {
        // The document says it is QML; which view is showing it does not.
        return qobject_cast<QmlJSEditorDocument *>(editor->document()) != nullptr;
    }

    bool supportsSorting() const final
    {
        return true;
    }

    TextEditor::IOutlineWidget *createWidget(Core::IEditor *editor) final
    {
        const auto document = qobject_cast<QmlJSEditorDocument *>(editor->document());
        QTC_ASSERT(document, return nullptr);
        return new QmlJSOutlineWidget(editor, document);
    }
};

void setupQmlJsOutline()
{
    static QmlJSOutlineWidgetFactory theQmlJSOutlineWidgetFactory;
}

#ifdef WITH_TESTS

// The outline pane follows the caret and moves it, and both are things any
// view of a document can do. It asked whether the editor was a QmlJSEditor, a
// class the Qt Quick path never builds, so a QML file in that view got no
// outline pane at all.
class QmlJSOutlineTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheOutlinePaneFollowsTheCaretInAnyView_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testTheOutlinePaneFollowsTheCaretInAnyView()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("qmljs-outline-pane");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("Outline.qml");
        QVERIFY(file.writeFileContents("import QtQuick\n"
                                       "Item {\n"
                                       "    Rectangle { id: box }\n"
                                       "    Text { id: label }\n"
                                       "}\n"));

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a QML file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);

        // Shown, because filling the outline model is work the document puts
        // off until a view says somebody is looking.
        editor->widget()->resize(400, 300);
        editor->widget()->show();
        const QScopeGuard hideIt([editor] { editor->widget()->hide(); });

        QmlJSOutlineWidgetFactory factory;
        QVERIFY2(factory.supportsEditor(editor),
                 "the outline does not offer itself for a QML file in this view");

        const std::unique_ptr<TextEditor::IOutlineWidget> outline(factory.createWidget(editor));
        QVERIFY2(outline.get(), "the outline built nothing");
        outline->setCursorSynchronization(true);

        auto * const tree = outline->findChild<QTreeView *>();
        QVERIFY(tree && tree->model());
        QTRY_VERIFY2(tree->model()->rowCount() > 0,
                     "the outline never listed anything in the file");

        auto * const document = qobject_cast<QmlJSEditorDocument *>(editor->document());
        QVERIFY(document);
        const QString text = document->plainText();

        // Into the Rectangle, and the pane has to say so.
        const auto caretAt = [editor, document, &text](const char *needle) {
            const int offset = text.indexOf(QLatin1String(needle));
            const Utils::Text::Position at
                = Utils::Text::Position::fromPositionInDocument(document->document(), offset);
            editor->gotoLine(at.line, at.column);
        };
        caretAt("Rectangle");
        QTRY_COMPARE(tree->currentIndex().data().toString(), QString("Rectangle"));

        // And back, so that what is asserted is the pane following rather than
        // one row happening to be current.
        caretAt("Text {");
        QTRY_COMPARE(tree->currentIndex().data().toString(), QString("Text"));

        // The other direction: choosing a row moves the caret, which is the
        // half of the outline a reader actually uses. Found by what it says,
        // because which row it is is not what this is about.
        QModelIndex boxRow;
        const auto findRow = [tree, &boxRow](const QModelIndex &parent, const auto &self) -> void {
            for (int row = 0; row < tree->model()->rowCount(parent); ++row) {
                const QModelIndex candidate = tree->model()->index(row, 0, parent);
                if (candidate.data().toString() == "Rectangle")
                    boxRow = candidate;
                else
                    self(candidate, self);
            }
        };
        findRow({}, findRow);
        QVERIFY2(boxRow.isValid(), "the outline lists no Rectangle");

        const int wasLine = editor->currentLine();
        tree->selectionModel()->select(boxRow, QItemSelectionModel::ClearAndSelect);
        QTRY_COMPARE(editor->currentLine(), 3);
        QVERIFY2(wasLine != 3, "the caret was already there, so this asserts nothing");
    }
};

QObject *createQmlJSOutlineTest()
{
    return new QmlJSOutlineTest;
}

#endif // WITH_TESTS

} // namespace QmlJSEditor::Internal

#ifdef WITH_TESTS
#include "qmljsoutline.moc"
#endif

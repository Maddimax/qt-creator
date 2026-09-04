// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "languageclientoutline.h"

#include "documentsymbolcache.h"
#include "languageclientmanager.h"
#include "languageclienttr.h"
#include "languageclientutils.h"

#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/find/itemviewfind.h>

#include <languageserverprotocol/languagefeatures.h>

#include <texteditor/ioutlinewidget.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/delegates.h>
#include <utils/dropsupport.h>
#include <utils/navigationtreeview.h>
#include <utils/treemodel.h>
#include <utils/treeviewcombobox.h>

#include <QAction>
#include <QBoxLayout>
#include <QMenu>
#include <QSortFilterProxyModel>

using namespace LanguageServerProtocol;

namespace LanguageClient {

static const QList<SymbolInformation> sortedSymbols(const QList<SymbolInformation> &symbols)
{
    return Utils::sorted(symbols, [](const SymbolInformation &a, const SymbolInformation &b){
        return a.location().range().start() < b.location().range().start();
    });
}

static const QList<DocumentSymbol> sortedSymbols(const QList<DocumentSymbol> &symbols)
{
    return Utils::sorted(symbols, [](const DocumentSymbol &a, const DocumentSymbol &b){
        return a.range().start() < b.range().start();
    });
}

class LanguageClientOutlineModel : public Utils::TreeModel<LanguageClientOutlineItem>
{
public:
    LanguageClientOutlineModel(Client *client) : m_client(client)  {}
    void setFilePath(const Utils::FilePath &filePath) { m_filePath = filePath; }

    void setInfo(const QList<SymbolInformation> &info, bool createOutOfScopeItem)
    {
        clear();
        if (createOutOfScopeItem)
            rootItem()->appendChild(new LanguageClientOutlineItem());
        for (const SymbolInformation &symbol : sortedSymbols(info))
            rootItem()->appendChild(new LanguageClientOutlineItem(symbol));
    }
    void setInfo(const QList<DocumentSymbol> &info, bool createOutOfScopeItem)
    {
        clear();
        if (createOutOfScopeItem)
            rootItem()->appendChild(new LanguageClientOutlineItem());
        for (const DocumentSymbol &symbol : sortedSymbols(info))
            rootItem()->appendChild(m_client->createOutlineItem(symbol));
    }

    Qt::DropActions supportedDragActions() const override
    {
        return Qt::MoveAction;
    }

    QStringList mimeTypes() const override
    {
        return Utils::DropSupport::mimeTypesForFilePaths();
    }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        auto mimeData = new Utils::DropMimeData;
        for (const QModelIndex &index : indexes) {
            if (LanguageClientOutlineItem *item = itemForIndex(index); item->valid()) {
                const LanguageServerProtocol::Position pos = item->pos();
                mimeData->addFile(m_filePath, pos.line() + 1, pos.character());
            }
        }
        return mimeData;
    }

private:
    Client * const m_client;
    Utils::FilePath m_filePath;
};

class DragSortFilterProxyModel final : public QSortFilterProxyModel
{
public:
    Qt::DropActions supportedDragActions() const final
    {
        return sourceModel()->supportedDragActions();
    }
};

class LanguageClientOutlineWidget final : public TextEditor::IOutlineWidget
{
public:
    LanguageClientOutlineWidget(Client *client,
                                Core::IEditor *editor,
                                TextEditor::TextDocument *document);

private:
    QList<QAction *> filterMenuActions() const final;
    void setCursorSynchronization(bool syncWithCursor) final;
    void setSorted(bool) final;
    bool isSorted() const final;
    void restoreSettings(const QVariantMap &map) final;
    QVariantMap settings() const final;

    void contextMenuEvent(QContextMenuEvent *event) final;

    void handleResponse(const DocumentUri &uri, const DocumentSymbolsResult &response);
    void updateTextCursor(const QModelIndex &proxyIndex);
    void updateSelectionInTree();
    void onItemActivated(const QModelIndex &index);

    QPointer<Client> m_client;
    // The editor rather than the widget: the caret is all this wants from the
    // view, and both views report and move one.
    QPointer<Core::IEditor> m_editor;
    LanguageClientOutlineModel m_model;
    DragSortFilterProxyModel m_proxyModel;
    Utils::NavigationTreeView m_view;
    Utils::AnnotatedItemDelegate m_delegate;
    DocumentUri m_uri;
    bool m_sync = false;
    bool m_sorted = false;
};

LanguageClientOutlineWidget::LanguageClientOutlineWidget(Client *client,
                                                         Core::IEditor *editor,
                                                         TextEditor::TextDocument *document)
    : m_client(client)
    , m_editor(editor)
    , m_model(client)
    , m_view(this)
    , m_uri(m_client->hostPathToServerUri(document->filePath()))
{
    connect(client->documentSymbolCache(),
            &DocumentSymbolCache::gotSymbols,
            this,
            &LanguageClientOutlineWidget::handleResponse);
    connect(client, &Client::documentUpdated, this, [this](TextEditor::TextDocument *document) {
        if (m_client && m_uri == m_client->hostPathToServerUri(document->filePath()))
            m_client->documentSymbolCache()->requestSymbols(m_uri, Schedule::Delayed);
    });

    client->documentSymbolCache()->requestSymbols(m_uri, Schedule::Delayed);

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(Core::ItemViewFind::createSearchableWrapper(&m_view));
    setLayout(layout);
    m_model.setFilePath(document->filePath());
    m_proxyModel.setSourceModel(&m_model);
    m_delegate.setDelimiter(" ");
    m_delegate.setAnnotationRole(LanguageClientOutlineItem::AnnotationRole);
    m_view.setModel(&m_proxyModel);
    m_view.setHeaderHidden(true);
    m_view.setExpandsOnDoubleClick(false);
    m_view.setFrameStyle(QFrame::NoFrame);
    m_view.setDragEnabled(true);
    m_view.setDragDropMode(QAbstractItemView::DragOnly);
    m_view.setItemDelegate(&m_delegate);
    connect(&m_view, &QAbstractItemView::activated,
            this, &LanguageClientOutlineWidget::onItemActivated);
    connect(m_editor, &Core::IEditor::cursorPositionChanged,
            this, &LanguageClientOutlineWidget::updateSelectionInTree);
    setFocusProxy(&m_view);
}

QList<QAction *> LanguageClientOutlineWidget::filterMenuActions() const
{
    return {};
}

void LanguageClientOutlineWidget::setCursorSynchronization(bool syncWithCursor)
{
    m_sync = syncWithCursor;
    updateSelectionInTree();
}

void LanguageClientOutlineWidget::setSorted(bool sorted)
{
    m_sorted = sorted;
    m_proxyModel.sort(sorted ? 0 : -1);
}

bool LanguageClientOutlineWidget::isSorted() const
{
    return m_sorted;
}

void LanguageClientOutlineWidget::restoreSettings(const QVariantMap &map)
{
    setSorted(map.value(QString("LspOutline.Sort"), false).toBool());
}

QVariantMap LanguageClientOutlineWidget::settings() const
{
    return {{QString("LspOutline.Sort"), m_sorted}};
}

void LanguageClientOutlineWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (!event)
        return;

    QMenu contextMenu;
    QAction *action = contextMenu.addAction(Tr::tr("Expand All"));
    connect(action, &QAction::triggered, &m_view, &QTreeView::expandAll);
    action = contextMenu.addAction(Tr::tr("Collapse All"));
    connect(action, &QAction::triggered, &m_view, &QTreeView::collapseAll);

    contextMenu.exec(event->globalPos());
    event->accept();
}

void LanguageClientOutlineWidget::handleResponse(const DocumentUri &uri,
                                                 const DocumentSymbolsResult &result)
{
    if (uri != m_uri)
        return;
    if (const auto i = std::get_if<QList<SymbolInformation>>(&result))
        m_model.setInfo(*i, false);
    else if (const auto s = std::get_if<QList<DocumentSymbol>>(&result))
        m_model.setInfo(*s, false);
    else
        m_model.clear();
    m_view.expandAll();

    // The list has changed, update the current items
    updateSelectionInTree();
}

void LanguageClientOutlineWidget::updateTextCursor(const QModelIndex &proxyIndex)
{
    LanguageClientOutlineItem *item = m_model.itemForIndex(m_proxyModel.mapToSource(proxyIndex));
    if (!item->valid())
        return;
    const Position &pos = item->pos();
    // line has to be 1 based, column 0 based!
    if (m_editor)
        m_editor->gotoLine(pos.line() + 1, pos.character(), true);
}

static LanguageClientOutlineItem *itemForPosition(const LanguageClientOutlineModel &m_model,
                                                 const Position &pos)
{
    LanguageClientOutlineItem *result = nullptr;
    m_model.forAllItems([&](LanguageClientOutlineItem *candidate){
        if (!candidate->valid() || !candidate->contains(pos))
            return;
        if (result && candidate->range().contains(result->range()))
            return; // skip item if the range is equal or bigger than the previous found range
        result = candidate;
    });
    return result;
}

void LanguageClientOutlineWidget::updateSelectionInTree()
{
    if (!m_sync || !m_editor)
        return;
    // IEditor counts both from one; the protocol counts both from zero.
    const Position caret(m_editor->currentLine() - 1, m_editor->currentColumn() - 1);
    if (LanguageClientOutlineItem *item = itemForPosition(m_model, caret)) {
        const QModelIndex index = m_proxyModel.mapFromSource(m_model.indexForItem(item));
        m_view.setCurrentIndex(index);
        m_view.scrollTo(index);
    } else {
        m_view.clearSelection();
    }
}

void LanguageClientOutlineWidget::onItemActivated(const QModelIndex &index)
{
    if (!index.isValid() || !m_editor)
        return;

    updateTextCursor(index);
    if (QWidget * const view = m_editor->widget())
        view->setFocus();
}

// The outline the toolbar shows for a file a language server knows about.
// A ToolBarOutline, so a view that draws its own toolbar has the model and the
// current row; it still fills a TreeViewComboBox, which is what the widget
// editor installs. Same division as CppEditorOutline.
class LanguageClientOutline : public TextEditor::ToolBarOutline
{
public:
    LanguageClientOutline(Client *client, Core::IEditor *editor);

    // The widget editor installs this and thereby takes it over; where nothing
    // does - a view that draws the outline itself - it stays ours to delete.
    ~LanguageClientOutline() override
    {
        if (m_combo && !m_combo->parentWidget())
            delete m_combo;
    }

    QWidget *widget() const { return m_combo; }

    QAbstractItemModel *model() const override
    {
        return const_cast<QSortFilterProxyModel *>(&m_proxyModel);
    }
    QModelIndex currentIndex() const override
    {
        return m_combo ? m_combo->view()->currentIndex() : QModelIndex();
    }
    QString currentText() const override { return m_combo ? m_combo->currentText() : QString(); }
    void activate(const QModelIndex &index) override
    {
        // Through the combo, so that picking a row from a form and picking one
        // from the combo are the same act.
        if (!m_combo)
            return;
        m_combo->view()->setCurrentIndex(index);
        activateEntry();
    }

private:
    void updateModel(const DocumentUri &resultUri, const DocumentSymbolsResult &result);
    void updateEntry();
    void activateEntry();
    void documentUpdated(TextEditor::TextDocument *document);
    void setSorted(bool sorted);

    TextEditor::TextDocument *document() const
    {
        return m_editor ? qobject_cast<TextEditor::TextDocument *>(m_editor->document())
                        : nullptr;
    }

    LanguageClientOutlineModel m_model;
    QSortFilterProxyModel m_proxyModel;
    QPointer<Client> m_client;
    QPointer<Core::IEditor> m_editor;
    // Ours until a view installs it, which reparents it away.
    QPointer<Utils::TreeViewComboBox> m_combo;
    const DocumentUri m_uri;
    Utils::AnnotatedItemDelegate m_delegate;
};

TextEditor::ToolBarOutline *createToolBarOutline(Client *client, Core::IEditor *editor)
{
    const auto document = qobject_cast<TextEditor::TextDocument *>(
        editor ? editor->document() : nullptr);
    if (client && document && client->supportsDocumentSymbols(document))
        return new LanguageClientOutline(client, editor);
    return nullptr;
}

QWidget *outlineWidget(TextEditor::ToolBarOutline *outline)
{
    // dynamic_cast rather than qobject_cast: the class is local to this file
    // and needs no meta-object of its own, only the base's signals.
    if (auto * const ours = dynamic_cast<LanguageClientOutline *>(outline))
        return ours->widget();
    return nullptr;
}

LanguageClientOutline::LanguageClientOutline(Client *client, Core::IEditor *editor)
    : ToolBarOutline(editor)
    , m_model(client)
    , m_client(client)
    , m_editor(editor)
    , m_combo(new Utils::TreeViewComboBox)
    , m_uri(client->hostPathToServerUri(
          qobject_cast<TextEditor::TextDocument *>(editor->document())->filePath()))
{
    m_proxyModel.setSourceModel(&m_model);
    const bool sorted = LanguageClientSettings::outlineComboBoxIsSorted();
    m_proxyModel.sort(sorted ? 0 : -1);
    m_combo->setModel(&m_proxyModel);
    m_delegate.setDelimiter(" ");
    m_delegate.setAnnotationRole(LanguageClientOutlineItem::AnnotationRole);
    m_combo->setItemDelegate(&m_delegate);
    m_combo->setMinimumContentsLength(13);
    QSizePolicy policy = m_combo->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    m_combo->setSizePolicy(policy);
    m_combo->setMaxVisibleItems(40);

    m_combo->setContextMenuPolicy(Qt::ActionsContextMenu);
    const QString sortActionText = Tr::tr("Sort Alphabetically");
    auto sortAction = new QAction(sortActionText, m_combo);
    sortAction->setCheckable(true);
    sortAction->setChecked(sorted);
    m_combo->addAction(sortAction);

    connect(client->documentSymbolCache(),
            &DocumentSymbolCache::gotSymbols,
            this,
            &LanguageClientOutline::updateModel);
    connect(client, &Client::documentUpdated, this, &LanguageClientOutline::documentUpdated);
    // The caret from whichever view: the widget's own signal would have left a
    // file in the Qt Quick editor with an outline that never followed it.
    connect(editor, &Core::IEditor::cursorPositionChanged,
            this, &LanguageClientOutline::updateEntry);
    connect(m_combo, &QComboBox::activated, this, &LanguageClientOutline::activateEntry);
    // What a view drawing the outline itself binds its label to.
    connect(m_combo, &QComboBox::currentIndexChanged,
            this, &TextEditor::ToolBarOutline::currentIndexChanged);
    connect(sortAction, &QAction::toggled, this, &LanguageClientOutline::setSorted);

    documentUpdated(document());
}

void LanguageClientOutline::updateModel(const DocumentUri &resultUri, const DocumentSymbolsResult &result)
{
    if (m_uri != resultUri)
        return;
    if (const auto i = std::get_if<QList<SymbolInformation>>(&result))
        m_model.setInfo(*i, true);
    else if (const auto s = std::get_if<QList<DocumentSymbol>>(&result))
        m_model.setInfo(*s, true);
    else
        m_model.clear();

    if (!m_combo)
        return;
    m_combo->view()->expandAll();
    // The list has changed, update the current item
    updateEntry();
}

void LanguageClientOutline::updateEntry()
{
    if (!m_editor || !m_combo)
        return;
    if (LanguageClientOutlineItem *item
            = itemForPosition(m_model, Position(TextEditor::textCursorOf(m_editor))))
        m_combo->setCurrentIndex(m_proxyModel.mapFromSource(m_model.indexForItem(item)));
    else
        m_combo->setCurrentIndex(m_proxyModel.mapFromSource(m_model.index(0,0)));

}

void LanguageClientOutline::activateEntry()
{
    if (!m_editor || !m_combo)
        return;
    const QModelIndex modelIndex = m_proxyModel.mapToSource(m_combo->view()->currentIndex());
    if (!modelIndex.isValid())
        return;
    LanguageClientOutlineItem *item = m_model.itemForIndex(modelIndex);
    if (!item->valid())
        return;
    const Position &pos = item->pos();
    Core::EditorManager::cutForwardNavigationHistory();
    Core::EditorManager::addCurrentPositionToNavigationHistory();
    // line has to be 1 based, column 0 based!
    // IEditor counts the line from one and gotoLine()'s column from zero,
    // which is what the protocol's character already is.
    m_editor->gotoLine(pos.line() + 1, pos.character(), true);
    Core::EditorManager::activateEditor(m_editor);
}

void LanguageClientOutline::documentUpdated(TextEditor::TextDocument *updated)
{
    if (updated && updated == document())
        m_client->documentSymbolCache()->requestSymbols(m_uri, Schedule::Delayed);
}

void LanguageClientOutline::setSorted(bool sorted)
{
    LanguageClientSettings::setOutlineComboBoxSorted(sorted);
    m_proxyModel.sort(sorted ? 0 : -1);
}

LanguageClientOutlineItem::LanguageClientOutlineItem(const SymbolInformation &info)
    : m_name(info.name())
    , m_range(info.location().range())
    , m_type(info.kind())
    , m_tags(info.symbolTags().value_or(QList<SymbolTag>()))
{ }

LanguageClientOutlineItem::LanguageClientOutlineItem(Client *client, const DocumentSymbol &info)
    : m_name(info.name())
    , m_detail(info.detail().value_or(QString()))
    , m_range(info.range())
    , m_selectionRange(info.selectionRange())
    , m_type(info.kind())
    , m_tags(info.symbolTags().value_or(QList<SymbolTag>()))
{
    const QList<LanguageServerProtocol::DocumentSymbol> children = sortedSymbols(
        info.children().value_or(QList<DocumentSymbol>()));
    for (const DocumentSymbol &child : children)
        appendChild(client->createOutlineItem(child));
}

QVariant LanguageClientOutlineItem::data(int column, int role) const
{
    switch (role) {
    case Qt::DecorationRole:
        return symbolIcon(m_type, m_tags);
    case Qt::DisplayRole:
        return valid() ? m_name : Tr::tr("<Select Symbol>");
    case AnnotationRole:
        return m_detail;
    default:
        return Utils::TreeItem::data(column, role);
    }
}
Qt::ItemFlags LanguageClientOutlineItem::flags(int column) const
{
    Q_UNUSED(column)
    return Utils::TypedTreeItem<LanguageClientOutlineItem>::flags(column) | Qt::ItemIsDragEnabled;
}

// LanguageClientOutlineWidgetFactory

class LanguageClientOutlineWidgetFactory final : public TextEditor::IOutlineWidgetFactory
{
public:
    using IOutlineWidgetFactory::IOutlineWidgetFactory;

public:
    bool supportsEditor(Core::IEditor *editor) const final
    {
        if (auto doc = qobject_cast<TextEditor::TextDocument *>(editor->document())) {
            if (Client *client = LanguageClientManager::clientForDocument(doc))
                return client->supportsDocumentSymbols(doc);
        }
        return false;
    }

    TextEditor::IOutlineWidget *createWidget(Core::IEditor *editor) final
    {
        const auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QTC_ASSERT(document, return nullptr);
        if (Client *client = LanguageClientManager::clientForDocument(document)) {
            if (client->supportsDocumentSymbols(document))
                return new LanguageClientOutlineWidget(client, editor, document);
        }
        return nullptr;
    }

    bool supportsSorting() const final { return true; }
};

void setupLanguageClientOutline()
{
    static LanguageClientOutlineWidgetFactory theLanguageClientOutlineWidgetFactory;
}

} // namespace LanguageClient

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "openeditorsview.h"

#include "documentmodel.h"
#include "editormanager.h"
#include "ieditor.h"
#include "opendocumentslist.h"
#include "opendocumentsproxymodel.h"
#include "../actionmanager/command.h"
#include "../coreplugintr.h"
#include "../inavigationwidgetfactory.h"
#include "../iversioncontrol.h"
#include "../opendocumentstreeview.h"
#include "../vcsmanager.h"

#include <utils/environment.h>
#include <utils/fsengine/fileiconprovider.h>
#include <utils/qtcassert.h>
#include <utils/theme/theme.h>
#include <utils/treemodel.h>

#include <QAbstractProxyModel>
#include <QApplication>
#include <QMenu>

using namespace Utils;

namespace Core::Internal {



// OpenEditorsWidget

class OpenEditorsWidget final : public OpenDocumentsTreeView
{
public:
    OpenEditorsWidget();
    ~OpenEditorsWidget() final;

private:
    void handleActivated(const QModelIndex &);
    void updateCurrentItem(IEditor*);
    void contextMenuRequested(QPoint pos);
    void activateEditor(const QModelIndex &index);
    void closeDocument(const QModelIndex &index);

    bool userWantsContextMenu(const QMouseEvent *) const final;

    ProxyModel *m_model;
};

OpenEditorsWidget::OpenEditorsWidget()
{
    setWindowTitle(Tr::tr("Open Documents"));
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);

    m_model = new ProxyModel(this);
    m_model->setSourceModel(DocumentModel::model());
    setModel(m_model);

    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
            this, &OpenEditorsWidget::updateCurrentItem);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old) {
        // re-sync after the user possibly changed the current item while
        // focus was in this view
        if (old == this) {
            QMetaObject::invokeMethod(
                this,
                [this] { updateCurrentItem(EditorManager::currentEditor()); },
                Qt::QueuedConnection);
        }
    });
    connect(this, &OpenDocumentsTreeView::activated, this, &OpenEditorsWidget::handleActivated);
    connect(this, &OpenDocumentsTreeView::closeActivated,
            this, &OpenEditorsWidget::closeDocument);

    connect(this, &OpenDocumentsTreeView::customContextMenuRequested,
            this, &OpenEditorsWidget::contextMenuRequested);
    updateCurrentItem(EditorManager::currentEditor());
}

OpenEditorsWidget::~OpenEditorsWidget() = default;

void OpenEditorsWidget::updateCurrentItem(IEditor *editor)
{
    if (!editor) {
        clearSelection();
        return;
    }
    const std::optional<int> index = DocumentModel::indexOfDocument(editor->document());
    QTC_ASSERT(index, return);
    const QModelIndex idx = m_model->index(*index, 0);
    if (idx == currentIndex())
        return;
    setCurrentIndex(idx);
    selectionModel()->select(currentIndex(),
                             QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    scrollTo(currentIndex());
}

void OpenEditorsWidget::handleActivated(const QModelIndex &index)
{
    if (index.column() == 0) {
        activateEditor(index);
    } else if (index.column() == 1) { // the funky close button
        closeDocument(index);

        // work around a bug in itemviews where the delegate wouldn't get the QStyle::State_MouseOver
        QPoint cursorPos = QCursor::pos();
        QWidget *vp = viewport();
        QMouseEvent e(QEvent::MouseMove, vp->mapFromGlobal(cursorPos), cursorPos, Qt::NoButton, {}, {});
        QCoreApplication::sendEvent(vp, &e);
    }
}

void OpenEditorsWidget::activateEditor(const QModelIndex &index)
{
    selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    EditorManager::activateEditorForEntry(
                DocumentModel::entryAtRow(m_model->mapToSource(index).row()));
}

void OpenEditorsWidget::closeDocument(const QModelIndex &index)
{
    EditorManager::closeDocuments({DocumentModel::entryAtRow(m_model->mapToSource(index).row())});
}

bool OpenEditorsWidget::userWantsContextMenu(const QMouseEvent *e) const
{
    // block activating on entry on right click otherwise we might switch into another mode
    // see QTCREATORBUG-30357
    return e->button() == Qt::RightButton;
}

void OpenEditorsWidget::contextMenuRequested(QPoint pos)
{
    QMenu contextMenu;
    QModelIndex editorIndex = indexAt(pos);
    const int row = m_model->mapToSource(editorIndex).row();
    DocumentModel::Entry *entry = DocumentModel::entryAtRow(row);
    EditorManager::addContextMenuActions(&contextMenu, entry, {}, EditorManager::ShowEditorActions);
    contextMenu.exec(mapToGlobal(pos));
}

// OpenEditorsViewFactory

class OpenEditorsViewFactory final : public INavigationWidgetFactory
{
public:
    OpenEditorsViewFactory()
    {
        setId("Open Documents");
        setDisplayName(Tr::tr("Open Documents"));
        setActivationSequence(QKeySequence(useMacShortcuts ? Tr::tr("Meta+O") : Tr::tr("Alt+O")));
        setPriority(200);
    }

    NavigationView createWidget() final
    {
        // The Qt Quick view is the sidebar now. Everything the tree view
        // does it does: the rows with their icon, version control colour and
        // tool tip, single-click and Return to open, Delete, Backspace and
        // the middle button to close, the right-click menu from the same
        // EditorManager call, the drag that carries the file, walking with
        // the arrows without opening anything, and the row following
        // whichever document is open.
        //
        // QTC_WIDGET_OPEN_DOCUMENTS asks for the tree view instead, and a
        // build with nothing to host QML gets it without asking.
        if (!Utils::qtcEnvironmentVariableIsSet("QTC_WIDGET_OPEN_DOCUMENTS")) {
            auto * const list = new OpenDocumentsList;
            if (QWidget * const view = createQmlView(
                    QUrl("qrc:/qt/qml/QtCreator/Core/OpenDocumentsView.qml"), list))
                return {view, {}};
            delete list;
        }
        return {new OpenEditorsWidget, {}};
    }
};

void createOpenEditorsViewFactory()
{
    static OpenEditorsViewFactory theOpenEditorsViewFactory;
}

} // Core::Internal

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppeditoroutline.h"

#include "cpptoolsreuse.h"

#include <utils/textutils.h>

#include <coreplugin/editormanager/ieditor.h>

#include "cppeditorconstants.h"
#include "cppeditordocument.h"
#include "cppeditortr.h"
#include "cppeditorwidget.h"
#include "cppoutlinemodel.h"

#include <texteditor/textdocument.h>

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>

#include <utils/storekey.h>
#include <utils/treeviewcombobox.h>

#include <QAction>
#include <QSortFilterProxyModel>
#include <QTimer>

/*!
    \class CppEditor::CppEditorOutline
    \brief A helper class that provides the outline model and widget,
           e.g. for the editor's tool bar.

    The caller is responsible for deleting the widget returned by widget().
 */

enum { UpdateOutlineIntervalInMs = 500 };

using namespace Core;
using namespace Utils;

namespace CppEditor::Internal {

class OutlineProxyModel final : public QSortFilterProxyModel
{
public:
    OutlineProxyModel(OutlineModel &sourceModel, QObject *parent)
        : QSortFilterProxyModel(parent)
        , m_sourceModel(sourceModel)
    {}

    bool filterAcceptsRow(int sourceRow,const QModelIndex &sourceParent) const final
    {
        // Ignore generated symbols, e.g. by macro expansion (Q_OBJECT)
        const QModelIndex sourceIndex = m_sourceModel.index(sourceRow, 0, sourceParent);
        if (m_sourceModel.isGenerated(sourceIndex))
            return false;

        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }
private:
    CppEditor::Internal::OutlineModel &m_sourceModel;
};

static Key sortEditorDocumentOutlineKey()
{
    return Key(Constants::CPPEDITOR_SETTINGSGROUP)
         + '/' + Constants::CPPEDITOR_SORT_EDITOR_DOCUMENT_OUTLINE;
}

const bool kSortEditorDocumentOutlineDefault = true;

static bool sortedEditorDocumentOutline()
{
    return ICore::settings()
        ->value(sortEditorDocumentOutlineKey(), kSortEditorDocumentOutlineDefault)
        .toBool();
}

static void setSortedEditorDocumentOutline(bool sorted)
{
    ICore::settings()->setValueWithDefault(sortEditorDocumentOutlineKey(),
                                           sorted,
                                           kSortEditorDocumentOutlineDefault);
}

QAbstractItemModel *CppEditorOutline::model() const
{
    return m_proxyModel;
}

QModelIndex CppEditorOutline::currentIndex() const
{
    return m_combo->view()->currentIndex();
}

QString CppEditorOutline::currentText() const
{
    return m_combo->currentText();
}

void CppEditorOutline::activate(const QModelIndex &index)
{
    // Through the combo, so that picking a row from a form and picking one
    // from the combo are the same act - including what it leaves selected.
    m_combo->view()->setCurrentIndex(index);
    gotoSymbolInEditor();
}

CppEditorOutline::CppEditorOutline(CppEditorWidget *widget)
    : ToolBarOutline(widget)
    , m_widget(widget)
    , m_combo(new TreeViewComboBox)
{
    m_model = &document()->outlineModel();
    build();
}

CppEditorOutline::CppEditorOutline(Core::IEditor *editor, CppEditorDocument *document)
    : ToolBarOutline(editor)
    , m_editor(editor)
    , m_document(document)
    , m_combo(new TreeViewComboBox)
{
    m_model = &document->outlineModel();
    build();
}

Core::IEditor *CppEditorOutline::editor() const
{
    return m_widget ? editorFor(m_widget) : m_editor;
}

CppEditorDocument *CppEditorOutline::document() const
{
    if (m_document)
        return m_document;
    return m_widget ? m_widget->cppEditorDocument() : nullptr;
}

void CppEditorOutline::build()
{
    m_proxyModel = new OutlineProxyModel(*m_model, this);
    m_proxyModel->setSourceModel(m_model);

    // Set up proxy model
    if (sortedEditorDocumentOutline())
        m_proxyModel->sort(0, Qt::AscendingOrder);
    else
        m_proxyModel->sort(-1, Qt::AscendingOrder); // don't sort yet, but set column for sortedOutline()
    m_proxyModel->setDynamicSortFilter(true);

    // Set up combo box
    m_combo->setModel(m_proxyModel);

    m_combo->setMinimumContentsLength(13);
    QSizePolicy policy = m_combo->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    m_combo->setSizePolicy(policy);
    m_combo->setMaxVisibleItems(40);

    m_combo->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_sortAction = new QAction(Tr::tr("Sort Alphabetically"), m_combo);
    m_sortAction->setCheckable(true);
    m_sortAction->setChecked(isSorted());
    connect(m_sortAction, &QAction::toggled, &setSortedEditorDocumentOutline);
    m_combo->addAction(m_sortAction);

    connect(m_combo, &QComboBox::activated, this, &CppEditorOutline::gotoSymbolInEditor);
    connect(m_combo, &QComboBox::currentIndexChanged, this, &CppEditorOutline::updateToolTip);

    connect(m_model, &OutlineModel::modelReset, this, &CppEditorOutline::updateNow);

    // Set up timers
    m_updateIndexTimer = new QTimer(this);
    m_updateIndexTimer->setObjectName("CppEditorOutline::m_updateIndexTimer");
    m_updateIndexTimer->setSingleShot(true);
    m_updateIndexTimer->setInterval(UpdateOutlineIntervalInMs);

    connect(m_updateIndexTimer, &QTimer::timeout, this, &CppEditorOutline::updateIndexNow);
}

bool CppEditorOutline::isSorted() const
{
    return m_proxyModel->sortColumn() == 0;
}

QWidget *CppEditorOutline::widget() const
{
    return m_combo;
}

void CppEditorOutline::updateNow()
{
    m_combo->view()->expandAll();
    updateIndexNow();
}

void CppEditorOutline::updateIndex()
{
    m_updateIndexTimer->start();
}

void CppEditorOutline::updateIndexNow()
{
    CppEditorDocument * const doc = document();
    if (!doc)
        return;
    if (m_model->editorRevision() != doc->document()->revision()) {
        doc->updateOutline();
        return;
    }

    m_updateIndexTimer->stop();

    // IEditor counts both from one; Text::Position counts the column from zero.
    Core::IEditor * const in = editor();
    if (!in)
        return;
    const Utils::Text::Position caret{in->currentLine(), in->currentColumn() - 1};
    if (QModelIndex comboIndex = m_model->indexForPosition(caret); comboIndex.isValid()) {
        QSignalBlocker blocker(m_combo);
        m_combo->setCurrentIndex(m_proxyModel->mapFromSource(comboIndex));
        updateToolTip();
        emit currentIndexChanged();
    }
}

void CppEditorOutline::updateToolTip()
{
    m_combo->setToolTip(m_combo->currentText());
}

void CppEditorOutline::gotoSymbolInEditor()
{
    const QModelIndex modelIndex = m_combo->view()->currentIndex();
    const QModelIndex sourceIndex = m_proxyModel->mapToSource(modelIndex);

    const Link link = m_model->linkFromIndex(sourceIndex);
    if (!link.hasValidTarget())
        return;

    EditorManager::cutForwardNavigationHistory();
    EditorManager::addCurrentPositionToNavigationHistory();
    Core::IEditor * const in = editor();
    if (!in)
        return;
    in->gotoLine(link.target.line, link.target.column, true);
    EditorManager::activateEditor(in);
}

} // namespace CppEditor::Internal

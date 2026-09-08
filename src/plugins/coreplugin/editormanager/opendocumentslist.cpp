// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "opendocumentslist.h"

#include "documentmodel.h"
#include "editormanager.h"
#include "ieditor.h"
#include "opendocumentsproxymodel.h"

#include <utils/qtcassert.h>

#include <QMenu>

namespace Core::Internal {

OpenDocumentsList::OpenDocumentsList(QObject *parent)
    : QObject(parent)
    , m_model(new ProxyModel(this))
{
    static_cast<ProxyModel *>(m_model)->setSourceModel(DocumentModel::model());
    connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
            this, &OpenDocumentsList::follow);
    follow(EditorManager::currentEditor());
}

QAbstractItemModel *OpenDocumentsList::model() const
{
    return m_model;
}

int OpenDocumentsList::currentRow() const
{
    return m_currentRow;
}

void OpenDocumentsList::activate(int row)
{
    DocumentModel::Entry * const entry = DocumentModel::entryAtRow(row + 1);
    QTC_ASSERT(entry, return);
    EditorManager::activateEditorForEntry(entry);
}

void OpenDocumentsList::close(int row)
{
    DocumentModel::Entry * const entry = DocumentModel::entryAtRow(row + 1);
    QTC_ASSERT(entry, return);
    EditorManager::closeDocuments({entry});
}

QObjectList OpenDocumentsList::contextMenuActions(int row)
{
    DocumentModel::Entry * const entry = DocumentModel::entryAtRow(row + 1);
    QTC_ASSERT(entry, return {});

    m_menu = std::make_unique<QMenu>();
    EditorManager::addContextMenuActions(m_menu.get(), entry, nullptr,
                                         EditorManager::ShowEditorActions);
    QObjectList actions;
    for (QAction * const action : m_menu->actions())
        actions << action;
    return actions;
}

void OpenDocumentsList::follow(IEditor *editor)
{
    const std::optional<int> row = editor ? DocumentModel::indexOfDocument(editor->document())
                                          : std::nullopt;
    const int now = row.value_or(-1);
    if (now == m_currentRow)
        return;
    m_currentRow = now;
    emit currentRowChanged();
}

} // namespace Core::Internal

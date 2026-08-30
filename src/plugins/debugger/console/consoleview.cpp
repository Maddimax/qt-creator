// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "consoleview.h"

#include "console.h"
#include "consoleitemmodel.h"
#include "../debuggertr.h"

#include <coreplugin/editormanager/editormanager.h>
#include <qtsupport/baseqtversion.h>

#include <utils/qtcassert.h>
#include <utils/stringutils.h>

#include <QAction>
#include <QQuickItem>
#include <QQuickWidget>
#include <QUrl>

namespace Debugger::Internal {

ConsoleView::ConsoleView(ConsoleItemModel *model, QAbstractItemModel *rows, QWidget *parent)
    : QtcQuick::QuickWidget(parent)
    , m_model(model)
    , m_rows(rows)
    , m_selection(new QItemSelectionModel(rows, this))
    , m_contextActions(new QtcQuick::ActionModel(this))
{
    m_copy = new QAction(Tr::tr("&Copy"), this);
    connect(m_copy, &QAction::triggered, this, [this] { copyRow(m_menuRow); });

    m_showInEditor = new QAction(Tr::tr("&Show in Editor"), this);
    connect(m_showInEditor, &QAction::triggered, this, [this] { openRow(m_menuRow); });

    m_clear = new QAction(Tr::tr("C&lear"), this);
    connect(m_clear, &QAction::triggered, this, [this] { m_model->clear(); });

    m_contextActions->setActions({m_copy, m_showInEditor, nullptr, m_clear});

    m_history.setModel(rows);

    quickWidget()->setInitialProperties({{"consoleRows", QVariant::fromValue(rows)},
                                         {"selection", QVariant::fromValue(m_selection)},
                                         {"pane", QVariant::fromValue(this)}});
    setSource(QUrl("qrc:/qt/qml/QtCreator/Debugger/ConsoleView.qml"));

    if (QObject * const root = rootObject()) {
        connect(root, SIGNAL(rowActivated(QVariant)), this, SLOT(onRowActivated(QVariant)));
        connect(root, SIGNAL(contextMenuRequested(QVariant)),
                this, SLOT(onContextMenuRequested(QVariant)));
    }
}

void ConsoleView::onScrollToBottom()
{
    QObject * const root = rootObject();
    QTC_ASSERT(root, return);
    QMetaObject::invokeMethod(root, "scrollToBottom");
}

void ConsoleView::populateFileFinder()
{
    QtSupport::QtVersion::populateQmlFileFinder(&m_finder, nullptr);
}

void ConsoleView::setCurrentIndex(const QModelIndex &index,
                                  QItemSelectionModel::SelectionFlags flags)
{
    m_selection->setCurrentIndex(index, flags);
    // The prompt has moved to a new row, so the history walk starts there.
    m_history.restart(index.row());
}

void ConsoleView::focusPrompt()
{
    QWidget::setFocus();
    QObject * const root = rootObject();
    QTC_ASSERT(root, return);
    QMetaObject::invokeMethod(root, "focusPrompt");
}

QString ConsoleView::historyUp(const QString &shown)
{
    return m_history.up(shown).value_or(shown);
}

QString ConsoleView::historyDown(const QString &shown)
{
    return m_history.down(shown).value_or(shown);
}

void ConsoleView::evaluate(const QString &expression)
{
    debuggerConsole()->evaluate(expression);
}

void ConsoleView::onRowActivated(const QVariant &index)
{
    openRow(index.toModelIndex());
}

void ConsoleView::onContextMenuRequested(const QVariant &index)
{
    m_menuRow = index.toModelIndex();
    m_copy->setEnabled(m_menuRow.isValid());
    m_showInEditor->setEnabled(canShowInEditor(m_menuRow));
}

void ConsoleView::openRow(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    const Utils::FilePaths candidates
        = m_finder.findFile(QUrl(m_rows->data(index, ConsoleItem::FileRole).toString()));
    if (candidates.isEmpty())
        return;

    // The finder answers with its best guess even when it did not really find
    // the file, and the row opens whatever that is if it can be read.
    const Utils::FilePath fp = candidates.constFirst();
    if (fp.isReadableFile())
        Core::EditorManager::openEditorAt({fp, m_rows->data(index, ConsoleItem::LineRole).toInt()});
}

void ConsoleView::copyRow(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    Utils::setClipboardAndSelection(
        ConsoleItem::copiedText(m_rows->data(index, ConsoleItem::ExpressionRole).toString(),
                                m_rows->data(index, ConsoleItem::FileRole).toString(),
                                m_rows->data(index, ConsoleItem::LineRole).toInt()));
}

bool ConsoleView::canShowInEditor(const QModelIndex &index) const
{
    if (!index.isValid())
        return false;

    bool success = false;
    m_finder.findFile(QUrl(m_rows->data(index, ConsoleItem::FileRole).toString()), &success);
    return success;
}

} // Debugger::Internal

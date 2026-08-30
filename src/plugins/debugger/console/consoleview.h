// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "consolehistory.h"

#include <qtcquick/actionmodel.h>
#include <qtcquick/qtcquickwidget.h>

#include <utils/fileinprojectfinder.h>

#include <QItemSelectionModel>

QT_BEGIN_NAMESPACE
class QAction;
QT_END_NAMESPACE

namespace Debugger::Internal {

class ConsoleItemModel;

class ConsoleView : public QtcQuick::QuickWidget
{
    Q_OBJECT

    Q_PROPERTY(QtcQuick::ActionModel *contextActions READ contextActions CONSTANT)

public:
    ConsoleView(ConsoleItemModel *model, QAbstractItemModel *rows, QWidget *parent);

    QtcQuick::ActionModel *contextActions() const { return m_contextActions; }

    void onScrollToBottom();
    void populateFileFinder();
    void setCurrentIndex(const QModelIndex &index, QItemSelectionModel::SelectionFlags flags);
    void focusPrompt();

    // The prompt is a text field in the QML, so it can neither walk the rows
    // for what was typed before nor run what is in it.
    Q_INVOKABLE QString historyUp(const QString &shown);
    Q_INVOKABLE QString historyDown(const QString &shown);
    Q_INVOKABLE void evaluate(const QString &expression);

private slots:
    void onRowActivated(const QVariant &index);
    void onContextMenuRequested(const QVariant &index);

private:
    void openRow(const QModelIndex &index);
    void copyRow(const QModelIndex &index);
    bool canShowInEditor(const QModelIndex &index) const;

    ConsoleItemModel * const m_model;
    QAbstractItemModel * const m_rows;
    QItemSelectionModel * const m_selection;
    QtcQuick::ActionModel * const m_contextActions;
    QAction *m_copy = nullptr;
    QAction *m_showInEditor = nullptr;
    QAction *m_clear = nullptr;
    // Which row the menu was opened over. A menu entry acts on that, not on
    // whatever the current row happens to be by the time it is picked.
    QPersistentModelIndex m_menuRow;
    ConsoleHistory m_history;
    Utils::FileInProjectFinder m_finder;
};

} // Debugger::Internal

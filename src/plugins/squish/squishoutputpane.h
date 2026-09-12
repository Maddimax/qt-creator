// Copyright (C) 2022 The Qt Company Ltd
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/ioutputpane.h>

#include <QItemSelectionModel>

QT_BEGIN_NAMESPACE
class QAction;
class QFrame;
class QLabel;
class QMenu;
class QAbstractItemModel;
class QModelIndex;
class QTabWidget;
class QToolButton;
QT_END_NAMESPACE

namespace Core { class OutputPaneView; }
namespace QtcQuick { class QuickWidget; }

namespace Squish::Internal {

class TestResult;
class SquishResultItem;
class SquishResultModel;
class SquishResultFilterModel;

class SquishOutputPane final : public Core::IOutputPane
{
    Q_OBJECT

    // Everything the QML touches has to be a property or an invokable: the
    // pane reaches it as an untyped context object, which hides plain
    // accessors from QML and from qmllint alike.
    Q_PROPERTY(QAbstractItemModel *rows READ rows CONSTANT)
    Q_PROPERTY(QItemSelectionModel *selection READ selection CONSTANT)

public:
    SquishOutputPane();

    static SquishOutputPane *instance();

    // IOutputPane interface
    QWidget *outputWidget(QWidget *parent) override;
    QList<ToolBarItem> toolBarItems() const override;
    void clearContents() override;
    void visibilityChanged(bool visible) override;
    void setFocus() override;
    bool hasFocus() const override;
    bool canFocus() const override;
    bool canNavigate() const override;
    bool canNext() const override;
    bool canPrevious() const override;
    void goToNext() override;
    void goToPrev() override;

    QAbstractItemModel *rows() const;
    QItemSelectionModel *selection() const { return m_selection; }

    // A row was opened. Given the index rather than the row, because which
    // row is which index is the view's knowledge in a tree.
    Q_INVOKABLE void activate(const QModelIndex &index);

    void addResultItem(SquishResultItem *item);
    void addLogOutput(const QString &output);
    void onTestRunFinished();
    void clearOldResults();

private:
    void createToolButtons();
    void initializeFilterMenu();
    void onItemActivated(const QModelIndex &idx);
    void onFilterMenuTriggered(QAction *action);
    void enableAllFiltersTriggered();
    void updateSummaryLabel();
    void setCurrent(const QModelIndex &index);

signals:
    // Asked of the view, which is the only thing that knows how a tree is
    // laid out in rows.
    void expandAllRequested();
    void collapseAllRequested();
    void expandRequested(const QModelIndex &index);

private:

    QTabWidget *m_outputPane;
    QWidget *m_outputWidget;
    QFrame *m_summaryWidget;
    QLabel *m_summaryLabel;
    QtcQuick::QuickWidget *m_resultsView;
    QItemSelectionModel *m_selection;
    SquishResultModel *m_model;
    SquishResultFilterModel *m_filterModel;
    Core::OutputPaneView *m_runnerServerLog;
    QAction *m_expandAllAction = nullptr;
    QAction *m_collapseAllAction = nullptr;
    QAction *m_filterAction = nullptr;
    QMenu *m_filterMenu;
};

void setupSquishOutputPane(QObject *guard);

} // namespace Squish::Internal

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/ioutputpane.h>

#include <qtcquick/actionmodel.h>

#include <memory>

namespace Utils { class FilePath; }

QT_BEGIN_NAMESPACE
class QAction;
class QModelIndex;
class QPoint;
QT_END_NAMESPACE

namespace ProjectExplorer {
class TaskCategory;
class TaskHub;
class Task;

namespace Internal {
class TaskWindowPrivate;

// Show issues (warnings or errors) and open the editor on click.
class TaskWindow final : public Core::IOutputPane
{
    Q_OBJECT

    // What the list binds to. A plain accessor is invisible to QML, and the
    // pane reaches it untyped, so nothing would say the binding was wrong.
    Q_PROPERTY(QAbstractItemModel *rows READ rows CONSTANT)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged)
    Q_PROPERTY(QList<int> selectedRows READ selectedRows NOTIFY selectedRowsChanged)
    Q_PROPERTY(QtcQuick::ActionModel *contextActions READ contextActions CONSTANT)


public:
    TaskWindow();
    ~TaskWindow() override;

    void delayedInitialization();

    int taskCount(Utils::Id category = Utils::Id()) const;
    int warningTaskCount(Utils::Id category = Utils::Id()) const;
    int errorTaskCount(Utils::Id category = Utils::Id()) const;

    // IOutputPane
    QWidget *outputWidget(QWidget *) override;

    // What the list shows and where it is. All three used to be the tree
    // view's: the current index, the selection the task handlers act on, and
    // the actions its context menu was built from.
    QAbstractItemModel *rows() const;
    int currentRow() const { return m_currentRow; }
    Q_INVOKABLE void setCurrentRow(int row);
    QList<int> selectedRows() const { return m_selectedRows; }
    Q_INVOKABLE void setSelectedRows(const QList<int> &rows);
    Q_INVOKABLE void activateRow(int row);
    QtcQuick::ActionModel *contextActions() const;

signals:
    void currentRowChanged();
    void selectedRowsChanged();

public:
    QList<QWidget *> toolBarWidgets() const override;

    void clearContents() override;
    void visibilityChanged(bool visible) override;

    bool canFocus() const override;
    bool hasFocus() const override;
    void setFocus() override;

    bool canNavigate() const override;
    bool canNext() const override;
    bool canPrevious() const override;
    void goToNext() override;
    void goToPrev() override;

signals:
    void tasksChanged();

private:
    void updateFilter() override;

    void addCategory(const TaskCategory &category);
    void removeCategory(Utils::Id categoryId);
    void addTask(const ProjectExplorer::Task &task);
    void removeTask(const ProjectExplorer::Task &task);
    void updatedTaskFilePath(const Task &task, const Utils::FilePath &fileName);
    void updatedTaskLineNumber(const Task &task, int line);
    void showTask(const Task &task);
    void openTask(const Task &task);
    void clearTasks(Utils::Id categoryId);
    void setCategoryVisibility(Utils::Id categoryId, bool visible);
    void saveSettings();
    void loadSettings();

    void goToNextOrPrev(int offset);

    int m_currentRow = -1;
    QList<int> m_selectedRows;

    void triggerDefaultHandler(const QModelIndex &index);
    void setShowWarnings(bool);
    void updateCategoriesMenu();

    int sizeHintForColumn(int column) const;

    const std::unique_ptr<TaskWindowPrivate> d;
};

} // namespace Internal
} // namespace ProjectExplorer

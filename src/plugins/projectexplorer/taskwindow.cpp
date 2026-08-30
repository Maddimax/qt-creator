// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "taskwindow.h"

#include "taskhandlers.h"
#include "parseissuesdialog.h"
#include "projectexplorericons.h"
#include "projectexplorertr.h"
#include "task.h"
#include "taskhub.h"
#include "taskmodel.h"

#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/find/itemviewfind.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/session.h>

#include <utils/algorithm.h>
#include <utils/fileinprojectfinder.h>
#include <utils/hostosinfo.h>
#include <utils/itemviews.h>
#include <utils/outputformatter.h>
#include <utils/qtcassert.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/tooltip/tooltip.h>
#include <utils/utilsicons.h>
#include <utils/widgets.h>

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <qtcquick/qtcquickwidget.h>

#include <QQuickWidget>

using namespace Core;
using namespace Utils;

const char SESSION_FILTER_CATEGORIES[] = "TaskWindow.Categories";
const char SESSION_FILTER_WARNINGS[] = "TaskWindow.IncludeWarnings";

namespace ProjectExplorer {

namespace Internal {

/////
// TaskWindow
/////

class TaskWindowPrivate
{
public:
    Internal::TaskModel *m_model = nullptr;
    Internal::TaskFilterModel *m_filter = nullptr;
    QPointer<QWidget> m_view;
    QList<QAction *> m_handlerActions;
    QtcQuick::ActionModel *m_contextActions = nullptr;
    const Core::Context m_taskWindowContext{Core::Context(Core::Constants::C_PROBLEM_PANE)};
    QToolButton *m_filterWarningsButton = nullptr;
    QToolButton *m_categoriesButton = nullptr;
    QToolButton *m_externalButton = nullptr;
    QMenu *m_categoriesMenu = nullptr;
    int m_visibleIssuesCount = 0;
};

static QToolButton *createFilterButton(const QIcon &icon, const QString &toolTip,
                                       QObject *receiver, std::function<void(bool)> lambda)
{
    auto button = new QToolButton;
    button->setIcon(icon);
    button->setToolTip(toolTip);
    button->setCheckable(true);
    button->setChecked(true);
    button->setEnabled(true);
    QObject::connect(button, &QToolButton::toggled, receiver, lambda);
    return button;
}

TaskWindow::TaskWindow() : d(std::make_unique<TaskWindowPrivate>())
{
    setId("Issues");
    setDisplayName(Tr::tr("Issues"));
    setPriorityInStatusBar(100);

    d->m_model = new Internal::TaskModel(this);
    d->m_filter = new Internal::TaskFilterModel(d->m_model);
    d->m_filter->setAutoAcceptChildRows(true);

    // What the list's context menu is drawn from. Asked again each time it
    // opens: the handlers register themselves the first time the pane is used.
    d->m_contextActions = new QtcQuick::ActionModel(this);
    d->m_contextActions->setProvider([this] {
        delayedInitialization();
        updateTaskHandlerActionsState();
        return d->m_handlerActions;
    });

    d->m_filterWarningsButton = createFilterButton(
                Utils::Icons::WARNING_TOOLBAR.icon(),
                Tr::tr("Show Warnings"), this, [this](bool show) { setShowWarnings(show); });

    d->m_externalButton = new QToolButton;
    d->m_externalButton->setIcon(Utils::Icons::OPENFILE_TOOLBAR.icon());
    d->m_externalButton->setToolTip(Tr::tr("Create Issues From External Build Output..."));
    connect(d->m_externalButton, &QToolButton::clicked, this, &executeParseIssuesDialog);

    d->m_categoriesButton = new QToolButton;
    d->m_categoriesButton->setIcon(Utils::Icons::FILTER.icon());
    d->m_categoriesButton->setToolTip(Tr::tr("Filter by categories"));
    d->m_categoriesButton->setProperty(StyleHelper::C_NO_ARROW, true);
    d->m_categoriesButton->setPopupMode(QToolButton::InstantPopup);

    d->m_categoriesMenu = new QMenu(d->m_categoriesButton);
    connect(d->m_categoriesMenu, &QMenu::aboutToShow, this, &TaskWindow::updateCategoriesMenu);
    Utils::addToolTipsToMenu(d->m_categoriesMenu);

    d->m_categoriesButton->setMenu(d->m_categoriesMenu);

    setupFilterUi("IssuesPane.Filter", "ProjectExplorer::Internal::TaskWindow");
    setFilteringEnabled(true);

    TaskHub *hub = &taskHub();
    connect(hub, &TaskHub::categoryAdded, this, &TaskWindow::addCategory);
    connect(hub, &TaskHub::categoryRemoved, this, &TaskWindow::removeCategory);
    connect(hub, &TaskHub::taskAdded, this, &TaskWindow::addTask);
    connect(hub, &TaskHub::taskRemoved, this, &TaskWindow::removeTask);
    connect(hub, &TaskHub::taskLineNumberUpdated, this, &TaskWindow::updatedTaskLineNumber);
    connect(hub, &TaskHub::taskFilePathUpdated, this, &TaskWindow::updatedTaskFilePath);
    connect(hub, &TaskHub::tasksCleared, this, &TaskWindow::clearTasks);
    connect(hub, &TaskHub::categoryVisibilityChanged, this, &TaskWindow::setCategoryVisibility);
    connect(hub, &TaskHub::popupRequested, this, &TaskWindow::popup, Qt::QueuedConnection);
    connect(hub, &TaskHub::showTask, this, &TaskWindow::showTask);
    connect(hub, &TaskHub::openTask, this, &TaskWindow::openTask);

    connect(d->m_filter, &TaskFilterModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex &, int first, int last) {
        d->m_visibleIssuesCount -= d->m_filter->issuesCount(first, last);
        emit setBadgeNumber(d->m_visibleIssuesCount);
    });
    connect(d->m_filter, &TaskFilterModel::rowsInserted, this,
            [this](const QModelIndex &, int first, int last) {
        d->m_visibleIssuesCount += d->m_filter->issuesCount(first, last);
        emit setBadgeNumber(d->m_visibleIssuesCount);
    });
    connect(d->m_filter, &TaskFilterModel::modelReset, this, [this] {
        d->m_visibleIssuesCount = d->m_filter->issuesCount(0, d->m_filter->rowCount());
        emit setBadgeNumber(d->m_visibleIssuesCount);
    });

    SessionManager *session = SessionManager::instance();
    connect(session, &SessionManager::aboutToSaveSession, this, &TaskWindow::saveSettings);
    connect(session, &SessionManager::sessionLoaded, this, &TaskWindow::loadSettings);
}

TaskWindow::~TaskWindow()
{
    delete d->m_externalButton;
    delete d->m_filterWarningsButton;
    delete d->m_filter;
    delete d->m_model;
}

void TaskWindow::delayedInitialization()
{
    static bool alreadyDone = false;
    if (alreadyDone)
        return;

    alreadyDone = true;

    const auto registerTaskHandlerAction = [this](QAction *action) {
        action->setParent(this);
        action->setEnabled(false);
        // Kept by the pane rather than added to a view: the list's context
        // menu is drawn from these, and the pane is what has them.
        d->m_handlerActions << action;
    };
    const auto getTasksForHandler = [this] {
        QModelIndexList indexes;
        for (const int row : std::as_const(m_selectedRows))
            indexes << d->m_filter->index(row, 0);
        return d->m_filter->tasks(indexes);
    };
    setupTaskHandlers(this, d->m_taskWindowContext, registerTaskHandlerAction, getTasksForHandler);
}

QList<QWidget*> TaskWindow::toolBarWidgets() const
{
    return {d->m_externalButton, d->m_filterWarningsButton, d->m_categoriesButton, filterWidget()};
}

QWidget *TaskWindow::outputWidget(QWidget *parent)
{
    if (!d->m_view) {
        auto widget = new QtcQuick::QuickWidget(parent);
        widget->quickWidget()->setInitialProperties({{"pane", QVariant::fromValue(this)}});
        widget->setSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/IssuesPane.qml"));
        // The pane's own context, which used to be attached to the tree view.
        Core::IContext::attach(widget, d->m_taskWindowContext);
        d->m_view = widget;
    }
    return d->m_view;
}

void TaskWindow::clearTasks(Id categoryId)
{
    d->m_model->clearTasks(categoryId);

    emit tasksChanged();
    navigateStateChanged();
}

void TaskWindow::setCategoryVisibility(Id categoryId, bool visible)
{
    if (!categoryId.isValid())
        return;

    QSet<Id> categories = d->m_filter->filteredCategories();

    if (visible)
        categories.remove(categoryId);
    else
        categories.insert(categoryId);

    d->m_filter->setFilteredCategories(categories);
}

void TaskWindow::saveSettings()
{
    const QStringList categories = Utils::toList(
        Utils::transform(d->m_filter->filteredCategories(), &Id::toString));
    SessionManager::setValue(SESSION_FILTER_CATEGORIES, categories);
    SessionManager::setValue(SESSION_FILTER_WARNINGS, d->m_filter->filterIncludesWarnings());
}

void TaskWindow::loadSettings()
{
    QVariant value = SessionManager::value(SESSION_FILTER_CATEGORIES);
    if (value.isValid()) {
        const QSet<Id> categories = Utils::toSet(
            Utils::transform(value.toStringList(), &Id::fromString));
        d->m_filter->setFilteredCategories(categories);
    }
    value = SessionManager::value(SESSION_FILTER_WARNINGS);
    if (value.isValid()) {
        bool includeWarnings = value.toBool();
        d->m_filter->setFilterIncludesWarnings(includeWarnings);
        d->m_filterWarningsButton->setChecked(d->m_filter->filterIncludesWarnings());
    }
}

void TaskWindow::visibilityChanged(bool visible)
{
    if (visible)
        delayedInitialization();
}

void TaskWindow::addCategory(const TaskCategory &category)
{
    d->m_model->addCategory(category);
    if (!category.visible) {
        QSet<Id> filters = d->m_filter->filteredCategories();
        filters.insert(category.id);
        d->m_filter->setFilteredCategories(filters);
    }
}

void TaskWindow::removeCategory(Id categoryId)
{
    d->m_model->removeCategory(categoryId);
    QSet<Id> filters = d->m_filter->filteredCategories();
    if (filters.remove(categoryId))
        d->m_filter->setFilteredCategories(filters);
}

void TaskWindow::addTask(const Task &task)
{
    d->m_model->addTask(task);

    emit tasksChanged();
    navigateStateChanged();

    if (task.isFlashworthy()
         && task.isError()
         && d->m_filter->filterIncludesErrors()
         && !d->m_filter->filteredCategories().contains(task.category())) {
        flash();
    }
}

void TaskWindow::removeTask(const Task &task)
{
    d->m_model->removeTask(task.id());

    emit tasksChanged();
    navigateStateChanged();
}

void TaskWindow::updatedTaskFilePath(const Task &task, const FilePath &filePath)
{
    d->m_model->updateTaskFilePath(task, filePath);
    emit tasksChanged();
}

void TaskWindow::updatedTaskLineNumber(const Task &task, int line)
{
    d->m_model->updateTaskLineNumber(task, line);
    emit tasksChanged();
}

void TaskWindow::showTask(const Task &task)
{
    int sourceRow = d->m_model->rowForTask(task);
    QModelIndex sourceIdx = d->m_model->index(sourceRow, 0);
    QModelIndex filterIdx = d->m_filter->mapFromSource(sourceIdx);
    setCurrentRow(filterIdx.row());
    popup(Core::IOutputPane::ModeSwitch);
}

void TaskWindow::openTask(const Task &task)
{
    int sourceRow = d->m_model->rowForTask(task);
    QModelIndex sourceIdx = d->m_model->index(sourceRow, 0);
    QModelIndex filterIdx = d->m_filter->mapFromSource(sourceIdx);
    triggerDefaultHandler(filterIdx);
}

void TaskWindow::triggerDefaultHandler(const QModelIndex &index)
{
    ITaskHandler * const defaultHandler = defaultTaskHandler();
    if (!index.isValid() || !defaultHandler)
        return;

    QModelIndex taskIndex = index;
    if (index.parent().isValid())
        taskIndex = index.parent();
    if (taskIndex.column() == 1)
        taskIndex = taskIndex.siblingAtColumn(0);
    Task task(d->m_filter->task(taskIndex));
    if (task.isNull())
        return;

    if (task.hasFile() && !task.file().isAbsolutePath()
            && !task.fileCandidates().empty()) {
        const FilePath userChoice = Utils::chooseFileFromList(task.fileCandidates());
        if (!userChoice.isEmpty()) {
            task.setFile(userChoice);
            updatedTaskFilePath(task, task.file());
        }
    }

    if (defaultHandler->canHandle(task)) {
        defaultHandler->handle(task);
    } else {
        if (!task.file().exists())
            d->m_model->setFileNotFound(taskIndex, true);
    }
}

void TaskWindow::setShowWarnings(bool show)
{
    d->m_filter->setFilterIncludesWarnings(show);
}

void TaskWindow::updateCategoriesMenu()
{
    d->m_categoriesMenu->clear();

    const QSet<Id> filteredCategories = d->m_filter->filteredCategories();
    const QList<TaskCategory> categories = Utils::sorted(d->m_model->categories(),
                                                         &TaskCategory::displayName);

    for (const TaskCategory &c : categories) {
        auto action = new QAction(d->m_categoriesMenu);
        action->setCheckable(true);
        action->setText(c.displayName);
        action->setToolTip(c.description);
        action->setChecked(!filteredCategories.contains(c.id));
        connect(action, &QAction::triggered, this, [this, action, id = c.id] {
            setCategoryVisibility(id, action->isChecked());
        });
        d->m_categoriesMenu->addAction(action);
    }
}

int TaskWindow::taskCount(Id category) const
{
    return d->m_model->taskCount(category);
}

int TaskWindow::errorTaskCount(Id category) const
{
    return d->m_model->errorTaskCount(category);
}

int TaskWindow::warningTaskCount(Id category) const
{
    return d->m_model->warningTaskCount(category);
}

void TaskWindow::clearContents()
{
    // clear all tasks in all displays
    // Yeah we are that special
    TaskHub::clearTasks();
}

bool TaskWindow::hasFocus() const
{
    return d->m_view && d->m_view->window()->focusWidget() == d->m_view;
}

bool TaskWindow::canFocus() const
{
    return d->m_filter->rowCount();
}

void TaskWindow::setFocus()
{
    if (d->m_filter->rowCount() == 0)
        return;
    if (d->m_view)
        d->m_view->setFocus();
    // Somewhere to start: the list opens on its first task, selected, which is
    // what the view's selection model was told to do here.
    if (m_currentRow < 0)
        setCurrentRow(0);
    if (m_selectedRows.isEmpty())
        setSelectedRows({m_currentRow});
}

bool TaskWindow::canNext() const
{
    return d->m_filter->rowCount();
}

bool TaskWindow::canPrevious() const
{
    return d->m_filter->rowCount();
}

void TaskWindow::goToNext()
{
    if (canNext())
        goToNextOrPrev(1);
}

void TaskWindow::goToPrev()
{
    if (canPrevious())
        goToNextOrPrev(-1);
}

void TaskWindow::goToNextOrPrev(int offset)
{
    const int row = taskRowAfter(d->m_filter, currentRow(), offset);
    if (row < 0)
        return;
    setCurrentRow(row);
    triggerDefaultHandler(d->m_filter->index(row, 0));
}

QAbstractItemModel *TaskWindow::rows() const
{
    return d->m_filter;
}

void TaskWindow::setCurrentRow(int row)
{
    if (m_currentRow == row)
        return;
    m_currentRow = row;
    emit currentRowChanged();
}

void TaskWindow::setSelectedRows(const QList<int> &rows)
{
    if (m_selectedRows == rows)
        return;
    m_selectedRows = rows;
    emit selectedRowsChanged();
    updateTaskHandlerActionsState();
}

void TaskWindow::activateRow(int row)
{
    setCurrentRow(row);
    triggerDefaultHandler(d->m_filter->index(row, 0));
}

QtcQuick::ActionModel *TaskWindow::contextActions() const
{
    return d->m_contextActions;
}

void TaskWindow::updateFilter()
{
    d->m_filter->updateFilterProperties(filterText(), filterCaseSensitivity(), filterUsesRegexp(),
                                        filterIsInverted());
}

bool TaskWindow::canNavigate() const
{
    return true;
}


} // namespace Internal
} // namespace ProjectExplorer

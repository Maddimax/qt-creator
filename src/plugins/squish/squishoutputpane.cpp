// Copyright (C) 2022 The Qt Company Ltd
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "squishoutputpane.h"

#include "squishresultmodel.h"
#include "squishtr.h"
#include "testresult.h"

#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QItemSelectionModel>
#include <QQuickWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/outputpaneview.h>

#include <qtcquick/qtcquickwidget.h>

#include <utils/itemviews.h>
#include <utils/stylehelper.h>
#include <utils/stylehelperpainting.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

namespace Squish::Internal {

static SquishOutputPane *m_instance = nullptr;

SquishOutputPane::SquishOutputPane()
{
    setId("Squish");
    setDisplayName(Tr::tr("Squish"));
    setPriorityInStatusBar(-60);

    m_instance = this;

    m_outputPane = new QTabWidget;
    m_outputPane->setDocumentMode(true);

    m_outputWidget = new QWidget;
    QVBoxLayout *outputLayout = new QVBoxLayout;
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(0);
    m_outputWidget->setLayout(outputLayout);

    QPalette pal;
    pal.setColor(QPalette::Window, Utils::creatorColor(Utils::Theme::InfoBarBackground));
    pal.setColor(QPalette::WindowText, Utils::creatorColor(Utils::Theme::InfoBarText));

    m_summaryWidget = new QFrame;
    m_summaryWidget->setPalette(pal);
    m_summaryWidget->setAutoFillBackground(true);
    QHBoxLayout *summaryLayout = new QHBoxLayout;
    summaryLayout->setContentsMargins(6, 6, 6, 6);
    m_summaryWidget->setLayout(summaryLayout);
    m_summaryLabel = new QLabel;
    m_summaryLabel->setPalette(pal);
    summaryLayout->addWidget(m_summaryLabel);
    m_summaryWidget->setVisible(false);

    outputLayout->addWidget(m_summaryWidget);

    m_model = new SquishResultModel(this);
    m_filterModel = new SquishResultFilterModel(m_model, this);
    m_filterModel->setDynamicSortFilter(true);

    // The pane's own, rather than a view's: navigating the results walks the
    // tree, and without a widget holding the current row there is nothing to
    // walk from.
    m_selection = new QItemSelectionModel(m_filterModel, this);

    m_resultsView = new QtcQuick::QuickWidget(m_outputWidget);
    m_resultsView->quickWidget()->setInitialProperties({{"pane", QVariant::fromValue(this)}});
    m_resultsView->setSource(QUrl("qrc:/qt/qml/QtCreator/Squish/SquishResults.qml"));

    outputLayout->addWidget(m_resultsView);

    createToolButtons();

    // A hundred characters a line at the block count the widget kept, which
    // is what this asked for before there was a character limit to ask for.
    m_runnerServerLog = new Core::OutputPaneView(Core::Context("Squish.RunnerServerLog"));
    m_runnerServerLog->setMaxCharCount(10000 * 100);

    m_outputPane->addTab(m_outputWidget, Tr::tr("Test Results"));
    m_outputPane->addTab(m_runnerServerLog, Tr::tr("Runner/Server Log"));

    connect(m_outputPane, &QTabWidget::currentChanged, this, [this] { navigateStateChanged(); });
    connect(m_model, &SquishResultModel::requestExpansion, this, [this](QModelIndex idx) {
        emit expandRequested(m_filterModel->mapFromSource(idx));
    });
    connect(m_model,
            &SquishResultModel::resultTypeCountUpdated,
            this,
            &SquishOutputPane::updateSummaryLabel);
}

SquishOutputPane *SquishOutputPane::instance()
{
    return m_instance;
}

QWidget *SquishOutputPane::outputWidget(QWidget *parent)
{
    if (m_outputPane)
        m_outputPane->setParent(parent);
    else
        qWarning("This should not happen");
    return m_outputPane;
}

QList<QWidget *> SquishOutputPane::toolBarWidgets() const
{
    return {m_filterButton, m_expandAll, m_collapseAll};
}

void SquishOutputPane::clearContents()
{
    if (m_outputPane->currentIndex() == 0)
        clearOldResults();
    else if (m_outputPane->currentIndex() == 1)
        m_runnerServerLog->clear();
}

void SquishOutputPane::visibilityChanged(bool visible)
{
    Q_UNUSED(visible)
}

void SquishOutputPane::setFocus()
{
    if (m_outputPane->currentIndex() == 0)
        m_resultsView->setFocus();
    else if (m_outputPane->currentIndex() == 1)
        m_runnerServerLog->setFocus();
}

bool SquishOutputPane::hasFocus() const
{
    // The focus is on the widget the Quick scene is in, which is inside the
    // log view rather than the view itself.
    const QWidget * const focused = m_outputPane->window()->focusWidget();
    return focused && (m_resultsView->isAncestorOf(focused)
                       || m_runnerServerLog->isAncestorOf(focused));
}

bool SquishOutputPane::canFocus() const
{
    return true;
}

bool SquishOutputPane::canNavigate() const
{
    return m_outputPane->currentIndex() == 0; // only support navigation for test results
}

bool SquishOutputPane::canNext() const
{
    return m_filterModel->hasResults();
}

bool SquishOutputPane::canPrevious() const
{
    return m_filterModel->hasResults();
}

void SquishOutputPane::goToNext()
{
    if (!canNext())
        return;

    const QModelIndex currentIndex = m_selection->currentIndex();
    QModelIndex nextCurrentIndex;

    if (currentIndex.isValid()) {
        // try to set next to first child or next sibling
        if (m_filterModel->rowCount(currentIndex)) {
            nextCurrentIndex = m_filterModel->index(0, 0, currentIndex);
        } else {
            nextCurrentIndex = currentIndex.sibling(currentIndex.row() + 1, 0);
            // if it had no sibling check siblings of parent (and grandparents if necessary)
            if (!nextCurrentIndex.isValid()) {
                QModelIndex parent = currentIndex.parent();
                do {
                    if (!parent.isValid())
                        break;
                    nextCurrentIndex = parent.sibling(parent.row() + 1, 0);
                    parent = parent.parent();
                } while (!nextCurrentIndex.isValid());
            }
        }
    }

    // if we have no current or could not find a next one, use the first item of the whole tree
    if (!nextCurrentIndex.isValid()) {
        Utils::TreeItem *rootItem = m_model->itemForIndex(QModelIndex());
        // if the tree does not contain any item - don't do anything
        if (!rootItem || !rootItem->childCount())
            return;

        nextCurrentIndex = m_filterModel->mapFromSource(m_model->indexForItem(rootItem->childAt(0)));
    }

    setCurrent(nextCurrentIndex);
}

void SquishOutputPane::goToPrev()
{
    if (!canPrevious())
        return;

    const QModelIndex currentIndex = m_selection->currentIndex();
    QModelIndex nextCurrentIndex;

    if (currentIndex.isValid()) {
        // try to set next to prior sibling or parent
        if (currentIndex.row() > 0) {
            nextCurrentIndex = currentIndex.sibling(currentIndex.row() - 1, 0);
            // if the sibling has children, use the last one
            while (int rowCount = m_filterModel->rowCount(nextCurrentIndex))
                nextCurrentIndex = m_filterModel->index(rowCount - 1, 0, nextCurrentIndex);
        } else {
            nextCurrentIndex = currentIndex.parent();
        }
    }

    // if we have no current or didn't find a sibling/parent use the last item of the whole tree
    if (!nextCurrentIndex.isValid()) {
        const QModelIndex rootIdx = m_filterModel->index(0, 0);
        // if the tree does not contain any item - don't do anything
        if (!rootIdx.isValid())
            return;

        // get the last (visible) top level index
        nextCurrentIndex = m_filterModel->index(m_filterModel->rowCount(QModelIndex()) - 1, 0);
        // step through until end
        while (int rowCount = m_filterModel->rowCount(nextCurrentIndex))
            nextCurrentIndex = m_filterModel->index(rowCount - 1, 0, nextCurrentIndex);
    }

    setCurrent(nextCurrentIndex);
}

QAbstractItemModel *SquishOutputPane::rows() const
{
    return m_filterModel;
}

void SquishOutputPane::activate(const QModelIndex &index)
{
    setCurrent(index);
}

void SquishOutputPane::setCurrent(const QModelIndex &index)
{
    m_selection->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect
                                            | QItemSelectionModel::Rows);
    onItemActivated(index);
}

void SquishOutputPane::addResultItem(SquishResultItem *item)
{
    m_model->addResultItem(item);
    if (!m_resultsView->isVisible())
        popup(Core::IOutputPane::NoModeSwitch);
    flash();
    navigateStateChanged();
}

void SquishOutputPane::addLogOutput(const QString &output)
{
    m_runnerServerLog->appendMessage(output + '\n', Utils::GeneralMessageFormat);
}

void SquishOutputPane::onTestRunFinished()
{
    m_model->expandVisibleRootItems();
    m_summaryWidget->setVisible(true);
    updateSummaryLabel();
}

void SquishOutputPane::updateSummaryLabel()
{
    if (m_summaryWidget->isVisible()) {
        const int passes = m_model->resultTypeCount(Result::Pass)
                           + m_model->resultTypeCount(Result::ExpectedFail);
        const int fails = m_model->resultTypeCount(Result::Fail)
                          + m_model->resultTypeCount(Result::UnexpectedPass);
        const QString labelText =
                QString("<p>" + Tr::tr("<b>Test summary:</b>&nbsp;&nbsp; %1 passes, %2 fails, "
                                     "%3 fatals, %4 errors, %5 warnings.") + "</p>")
                                      .arg(passes)
                                      .arg(fails)
                                      .arg(m_model->resultTypeCount(Result::Fatal))
                                      .arg(m_model->resultTypeCount(Result::Error))
                                      .arg(m_model->resultTypeCount(Result::Warn));

        m_summaryLabel->setText(labelText);
    }
}

void SquishOutputPane::clearOldResults()
{
    m_summaryWidget->setVisible(false);
    m_filterModel->clearResults();
    navigateStateChanged();
}

void SquishOutputPane::createToolButtons()
{
    m_expandAll = new QToolButton(m_resultsView);
    Utils::StyleHelper::setPanelWidget(m_expandAll);
    m_expandAll->setIcon(Utils::Icons::EXPAND_TOOLBAR.icon());
    m_expandAll->setToolTip(Tr::tr("Expand All"));

    m_collapseAll = new QToolButton(m_resultsView);
    Utils::StyleHelper::setPanelWidget(m_collapseAll);
    m_collapseAll->setIcon(Utils::Icons::COLLAPSE_TOOLBAR.icon());
    m_collapseAll->setToolTip(Tr::tr("Collapse All"));

    m_filterButton = new QToolButton(m_resultsView);
    Utils::StyleHelper::setPanelWidget(m_filterButton);
    m_filterButton->setIcon(Utils::Icons::FILTER.icon());
    m_filterButton->setToolTip(Tr::tr("Filter Test Results"));
    m_filterButton->setProperty(Utils::StyleHelper::C_NO_ARROW, true);
    m_filterButton->setAutoRaise(true);
    m_filterButton->setPopupMode(QToolButton::InstantPopup);
    m_filterMenu = new QMenu(m_filterButton);
    initializeFilterMenu();
    m_filterButton->setMenu(m_filterMenu);

    connect(m_expandAll, &QToolButton::clicked,
            this, &SquishOutputPane::expandAllRequested);
    connect(m_collapseAll, &QToolButton::clicked,
            this, &SquishOutputPane::collapseAllRequested);
    connect(m_filterMenu, &QMenu::triggered, this, &SquishOutputPane::onFilterMenuTriggered);
}

void SquishOutputPane::initializeFilterMenu()
{
    QMap<Result::Type, QString> textAndType;
    textAndType.insert(Result::Pass, Tr::tr("Pass"));
    textAndType.insert(Result::Fail, Tr::tr("Fail"));
    textAndType.insert(Result::ExpectedFail, Tr::tr("Expected Fail"));
    textAndType.insert(Result::UnexpectedPass, Tr::tr("Unexpected Pass"));
    textAndType.insert(Result::Warn, Tr::tr("Warning Messages"));
    textAndType.insert(Result::Log, Tr::tr("Log Messages"));

    const QList<Result::Type> types = textAndType.keys();
    for (Result::Type type : types) {
        QAction *action = new QAction(m_filterMenu);
        action->setText(textAndType.value(type));
        action->setCheckable(true);
        action->setChecked(true);
        action->setData(type);
        m_filterMenu->addAction(action);
    }
    m_filterMenu->addSeparator();
    QAction *action = new QAction(m_filterMenu);
    action->setText(Tr::tr("Check All Filters"));
    action->setCheckable(false);
    m_filterMenu->addAction(action);
    connect(action, &QAction::triggered, this, &SquishOutputPane::enableAllFiltersTriggered);
}

void SquishOutputPane::onItemActivated(const QModelIndex &idx)
{
    if (!idx.isValid())
        return;

    const TestResult result = m_filterModel->testResult(idx);
    if (!result.file().isEmpty())
        Core::EditorManager::openEditorAt(
            Utils::Link(Utils::FilePath::fromString(result.file()), result.line(), 0));
}


void SquishOutputPane::onFilterMenuTriggered(QAction *action)
{
    m_filterModel->toggleResultType(Result::Type(action->data().toInt()));
    navigateStateChanged();
}

void SquishOutputPane::enableAllFiltersTriggered()
{
    const QList<QAction *> actions = m_filterMenu->actions();
    for (QAction *action : actions)
        action->setChecked(true);

    m_filterModel->enableAllResultTypes();
}

void setupSquishOutputPane(QObject *guard)
{
    m_instance = new SquishOutputPane;
    m_instance->setParent(guard);
}

} // namespace Squish::Internal

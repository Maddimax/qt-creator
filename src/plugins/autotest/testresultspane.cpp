// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "testresultspane.h"

#include "autotesticons.h"
#include "autotestplugin.h"
#include "autotesttr.h"
#include "itestframework.h"
#include "testeditormark.h"
#include "testresultmodel.h"
#include "testresultmodel.h"
#include "testrunner.h"
#include "testsettings.h"
#include "testtreeitem.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/find/itemviewfind.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/outputpaneview.h>
#include <coreplugin/session.h>

#include <projectexplorer/projectexplorer.h>

#include <texteditor/fontsettings.h>
#include <texteditor/texteditor.h>

#include <utils/filedialogs.h>
#include <utils/async.h>
#include <utils/fileutils.h>
#include <utils/proxyaction.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QDebug>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QCursor>
#include <QQuickItem>
#include <QQuickWidget>
#include <QItemSelectionModel>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QStandardItemModel>
#include <QTest>
#endif

using namespace Core;
using namespace Utils;

namespace Autotest::Internal {

// What Core::ItemViewFind asks of a view. Owned by the finder, which is why it
// is separate from the view rather than the view answering for itself.
class TestResultsFindTarget : public Core::ItemViewTarget
{
public:
    explicit TestResultsFindTarget(TestResultsView *view)
        : m_view(view)
    {}

    QAbstractItemModel *model() const override { return m_view->resultModel(); }
    QModelIndex currentIndex() const override { return m_view->currentIndex(); }
    void setCurrentIndex(const QModelIndex &index) override { m_view->setCurrentIndex(index); }
    void reveal(const QModelIndex &index) override { m_view->reveal(index); }
    QWidget *widget() const override { return m_view; }

private:
    const QPointer<TestResultsView> m_view;
};

TestResultsView::TestResultsView(QAbstractItemModel *model, QWidget *parent)
    : QtcQuick::QuickWidget(parent)
    , m_model(model)
    , m_selection(new QItemSelectionModel(model, this))
{
    quickWidget()->setInitialProperties(
        {{"resultRows", QVariant::fromValue(model)},
         {"selection", QVariant::fromValue(m_selection)}});
    setSource(QUrl("qrc:/qt/qml/QtCreator/AutoTest/TestResultsView.qml"));

    if (QObject * const root = rootObject()) {
        connect(root, SIGNAL(rowActivated(QVariant)), this, SLOT(onRowActivated(QVariant)));
        connect(root, SIGNAL(contextMenuRequested(QVariant)),
                this, SLOT(onContextMenuRequested(QVariant)));
        connect(root, SIGNAL(copyRequested()), this, SIGNAL(copyShortcutTriggered()));
    }
}

void TestResultsView::onRowActivated(const QVariant &index)
{
    emit activated(index.toModelIndex());
}

void TestResultsView::onContextMenuRequested(const QVariant &index)
{
    emit contextMenuRequested(index.toModelIndex());
}

QQuickItem *TestResultsView::treeItem() const
{
    auto * const root = qobject_cast<QQuickItem *>(rootObject());
    QTC_ASSERT(root, return nullptr);
    return root->findChild<QQuickItem *>("testResults");
}

int TestResultsView::rowOf(const QModelIndex &index) const
{
    QQuickItem * const tree = treeItem();
    if (!tree || !index.isValid())
        return -1;
    int row = -1;
    // A tree's rows are the view's own arithmetic: which row an index is on
    // depends on what is open above it, so only the view can say.
    QMetaObject::invokeMethod(tree, "rowAtIndex", Q_RETURN_ARG(int, row),
                              Q_ARG(QModelIndex, index));
    return row;
}

QModelIndex TestResultsView::currentIndex() const
{
    return m_selection->currentIndex();
}

void TestResultsView::setCurrentIndex(const QModelIndex &index)
{
    m_selection->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect
                                            | QItemSelectionModel::Rows);
}

void TestResultsView::reveal(const QModelIndex &index)
{
    if (QQuickItem * const tree = treeItem())
        QMetaObject::invokeMethod(tree, "expandToIndex", Q_ARG(QModelIndex, index));
}

bool TestResultsView::isExpanded(const QModelIndex &index) const
{
    QQuickItem * const tree = treeItem();
    const int row = rowOf(index);
    if (!tree || row < 0)
        return false;
    bool expanded = false;
    QMetaObject::invokeMethod(tree, "isExpanded", Q_RETURN_ARG(bool, expanded),
                              Q_ARG(int, row));
    return expanded;
}

void TestResultsView::expand(const QModelIndex &index)
{
    QQuickItem * const tree = treeItem();
    const int row = rowOf(index);
    if (tree && row >= 0)
        QMetaObject::invokeMethod(tree, "expand", Q_ARG(int, row));
}

void TestResultsView::expandRecursively(const QModelIndex &index)
{
    QQuickItem * const tree = treeItem();
    const int row = rowOf(index);
    if (tree && row >= 0)
        QMetaObject::invokeMethod(tree, "expandRecursively", Q_ARG(int, row), Q_ARG(int, -1));
}

void TestResultsView::expandAll()
{
    if (QQuickItem * const tree = treeItem())
        QMetaObject::invokeMethod(tree, "expandRecursively", Q_ARG(int, -1), Q_ARG(int, -1));
}

void TestResultsView::collapseAll()
{
    if (QQuickItem * const tree = treeItem())
        QMetaObject::invokeMethod(tree, "collapseRecursively");
}

void TestResultsView::setShowDuration(bool show)
{
    if (QObject * const root = rootObject())
        root->setProperty("showDuration", show);
}

bool TestResultsView::isAtEnd() const
{
    QQuickItem * const tree = treeItem();
    return tree ? tree->property("atYEnd").toBool() : true;
}

void TestResultsView::scrollToEnd()
{
    QQuickItem * const tree = treeItem();
    if (!tree)
        return;
    const qreal bottom = tree->property("contentHeight").toReal()
                         - tree->property("height").toReal();
    tree->setProperty("contentY", std::max(qreal(0), bottom));
}

TestResultsPane::TestResultsPane(QObject *parent) :
    IOutputPane(parent),
    m_context(new IContext(this))
{
    setId("TestResults");
    setDisplayName(Tr::tr("Test Results"));
    setPriorityInStatusBar(-30);
    m_outputWidget = new QStackedWidget;
    QWidget *visualOutputWidget = new QWidget;
    m_outputWidget->addWidget(visualOutputWidget);
    QVBoxLayout *outputLayout = new QVBoxLayout;
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(0);
    visualOutputWidget->setLayout(outputLayout);

    QPalette pal;
    pal.setColor(QPalette::Window, creatorColor(Theme::InfoBarBackground));
    pal.setColor(QPalette::WindowText, creatorColor(Theme::InfoBarText));
    m_summaryWidget = new QFrame;
    m_summaryWidget->setPalette(pal);
    m_summaryWidget->setAutoFillBackground(true);
    QHBoxLayout *layout = new QHBoxLayout;
    layout->setContentsMargins(6, 6, 6, 6);
    m_summaryWidget->setLayout(layout);
    m_summaryLabel = new QLabel;
    m_summaryLabel->setPalette(pal);
    layout->addWidget(m_summaryLabel);
    m_summaryWidget->setVisible(false);

    outputLayout->addWidget(m_summaryWidget);

    m_model = new TestResultModel(this);
    m_filterModel = new TestResultFilterModel(this);
    m_filterModel->setSourceModel(m_model);
    m_filterModel->setDynamicSortFilter(true);
    m_filterModel->setRecursiveFilteringEnabled(true);

    m_treeView = new TestResultsView(m_filterModel, visualOutputWidget);

    // Ctrl+F over the results, through the same wrapper a widget view got: it
    // needs somewhere to put a find bar and something that can answer for a
    // view, and neither has to be a QAbstractItemView any more.
    outputLayout->addWidget(ItemViewFind::createSearchableWrapper(
        new ItemViewFind(new TestResultsFindTarget(m_treeView), Qt::DisplayRole)));

    m_textOutput = new Core::OutputPaneView(Core::Context("AutoTest.TextOutput"),
                                            "AutoTest.TextOutput.Filter");

    m_textOutput->setBaseFont(TextEditor::globalFontSettings().data().font());
    m_textOutput->setWordWrapEnabled(true);
    m_outputWidget->addWidget(m_textOutput);

    setupFilterUi("AutoTest.TextOutput.Filter", "Autotest::Internal::TestResultsPane");
    setupContext("AutoTest.TextOutput", m_textOutput);
    setFilteringEnabled(true);
    setZoomButtonsEnabled(false);
    connect(this, &IOutputPane::zoomInRequested, m_textOutput, &Core::OutputPaneView::zoomIn);
    connect(this, &IOutputPane::zoomOutRequested, m_textOutput, &Core::OutputPaneView::zoomOut);
    connect(this, &IOutputPane::resetZoomRequested, m_textOutput, &Core::OutputPaneView::resetZoom);
    connect(this, &IOutputPane::fontChanged, m_textOutput, &Core::OutputPaneView::setBaseFont);

    createToolButtons();

    connect(m_treeView, &TestResultsView::activated, this, &TestResultsPane::onItemActivated);
    connect(m_treeView, &TestResultsView::contextMenuRequested,
            this, &TestResultsPane::onCustomContextMenuRequested);
    connect(m_treeView, &TestResultsView::copyShortcutTriggered, this, [this] {
        onCopyItemTriggered(getTestResult(m_treeView->currentIndex()));
    });
    connect(m_model, &TestResultModel::requestExpansion, this, [this](const QModelIndex &idx) {
        m_treeView->expandRecursively(m_filterModel->mapFromSource(idx));
    });
    connect(TestRunner::instance(), &TestRunner::testRunStarted,
            this, &TestResultsPane::onTestRunStarted);
    connect(TestRunner::instance(), &TestRunner::testRunFinished,
            this, &TestResultsPane::onTestRunFinished);
    connect(TestRunner::instance(), &TestRunner::testResultReady,
            this, &TestResultsPane::scheduleTestResult);
    connect(TestRunner::instance(), &TestRunner::hadDisabledTests,
            m_model, &TestResultModel::raiseDisabledTests);
    connect(SessionManager::instance(), &SessionManager::sessionLoaded,
            this, &TestResultsPane::onSessionLoaded);
    connect(SessionManager::instance(), &SessionManager::aboutToSaveSession,
            this, &TestResultsPane::onAboutToSaveSession);

    m_bufferTimer.setSingleShot(true);
    m_bufferTimer.setInterval(150);
    connect(&m_bufferTimer, &QTimer::timeout, this, &TestResultsPane::handleNextBuffered);
}

void TestResultsPane::createToolButtons()
{
    m_expandCollapse = new QToolButton(m_treeView);
    m_expandCollapse->setIcon(Utils::Icons::EXPAND_ALL_TOOLBAR.icon());
    m_expandCollapse->setToolTip(Tr::tr("Expand All"));
    m_expandCollapse->setCheckable(true);
    m_expandCollapse->setChecked(false);
    connect(m_expandCollapse, &QToolButton::clicked, this, [this](bool checked) {
        if (checked)
            m_treeView->expandAll();
        else
            m_treeView->collapseAll();
    });

    m_runAll = new QToolButton(m_treeView);
    m_runAll->setDefaultAction(ProxyAction::proxyActionWithIcon(
                    ActionManager::command(Constants::ACTION_RUN_ALL_ID)->action(),
                    Utils::Icons::RUN_SMALL_TOOLBAR.icon()));

    m_runSelected = new QToolButton(m_treeView);
    m_runSelected->setDefaultAction(ProxyAction::proxyActionWithIcon(
                    ActionManager::command(Constants::ACTION_RUN_SELECTED_ID)->action(),
                    Utils::Icons::RUN_SELECTED_TOOLBAR.icon()));

    m_runFailed = new QToolButton(m_treeView);
    m_runFailed->setDefaultAction(ProxyAction::proxyActionWithIcon(
                    ActionManager::command(Constants::ACTION_RUN_FAILED_ID)->action(),
                    Icons::RUN_FAILED_TOOLBAR.icon()));
    m_runFile = new QToolButton(m_treeView);
    m_runFile->setDefaultAction(ProxyAction::proxyActionWithIcon(
                    ActionManager::command(Constants::ACTION_RUN_FILE_ID)->action(),
                    Utils::Icons::RUN_FILE_TOOLBAR.icon()));

    m_stopTestRun = new QToolButton(m_treeView);
    m_stopTestRun->setIcon(Utils::Icons::STOP_SMALL_TOOLBAR.icon());
    m_stopTestRun->setToolTip(Tr::tr("Stop Test Run"));
    m_stopTestRun->setEnabled(false);
    connect(m_stopTestRun, &QToolButton::clicked, TestRunner::instance(), &TestRunner::requestStopTestRun);

    m_filterButton = new QToolButton(m_treeView);
    m_filterButton->setIcon(Utils::Icons::FILTER.icon());
    m_filterButton->setToolTip(Tr::tr("Filter Test Results"));
    m_filterButton->setProperty(StyleHelper::C_NO_ARROW, true);
    m_filterButton->setPopupMode(QToolButton::InstantPopup);
    m_filterMenu = new QMenu(m_filterButton);
    initializeFilterMenu();
    connect(m_filterMenu, &QMenu::triggered, this, &TestResultsPane::filterMenuTriggered);
    m_filterButton->setMenu(m_filterMenu);
    m_outputToggleButton = new QToolButton(m_treeView);
    m_outputToggleButton->setIcon(Icons::TEXT_DISPLAY.icon());
    m_outputToggleButton->setToolTip(Tr::tr("Switch Between Visual and Text Display"));
    m_outputToggleButton->setEnabled(true);
    connect(m_outputToggleButton, &QToolButton::clicked, this, &TestResultsPane::toggleOutputStyle);
    const auto stopwatch = Utils::Icon({{":/utils/images/stopwatch.png",
                                         Utils::Theme::IconsBaseColor}});
    m_showDuration.setDefaultValue(true);
    m_showDuration.setLabelText(Tr::tr("Show Durations"));
    m_showDuration.setToolTip(Tr::tr("Show Durations"));
    m_showDuration.setIcon(stopwatch.icon());
    m_showDuration.setValue(true);
    connect(&m_showDuration, &Utils::BoolAspect::changed, this, [this] {
        m_treeView->setShowDuration(m_showDuration());
    });
}

static TestResultsPane *s_instance = nullptr;

TestResultsPane *TestResultsPane::instance()
{
    if (!s_instance)
        s_instance = new TestResultsPane;
    return s_instance;
}

TestResultsPane::~TestResultsPane()
{
    delete m_treeView;
    if (!m_outputWidget->parent())
        delete m_outputWidget;
    s_instance = nullptr;
}

void TestResultsPane::scheduleTestResult(const TestResult &result)
{
    if (result.result() == ResultType::MessageCurrentTest) {
        m_lastCurrentMessage.emplace(result);
    } else {
        m_buffered.enqueue(result);
        m_model->raiseTestResultCount(result.id(), result.result()); // needed for correct summary
    }
    if (!m_bufferTimer.isActive())
        m_bufferTimer.start();
}

void TestResultsPane::handleNextBuffered()
{
    if (m_lastCurrentMessage) {
        addTestResult(*m_lastCurrentMessage);
        m_lastCurrentMessage.reset();
    }
    for (int i = 0, end = qMin(30, m_buffered.size()); i < end; ++i)
        addTestResult(m_buffered.dequeue());

    if (!m_testRunning && m_buffered.size() > 30) {
        handlePendingResultsSilently();
        return;
    }

    if (!m_buffered.isEmpty()) {
        m_bufferTimer.start();
    } else if (!m_testRunning) {
        createMarks();
        updateMenuItemsEnabledState();
    }
}

void TestResultsPane::addTestResult(const TestResult &result)
{
    m_atEnd = m_treeView->isAtEnd();

    m_model->addTestResult(result, m_expandCollapse->isChecked());
    setIconBadgeNumber(m_model->resultTypeCount(ResultType::Fail)
                       + m_model->resultTypeCount(ResultType::MessageFatal)
                       + m_model->resultTypeCount(ResultType::UnexpectedPass));
    flash();
    navigateStateChanged();
}


void TestResultsPane::addOutputLine(const QByteArray &outputLine, OutputChannel channel)
{
    if (QTC_UNEXPECTED(outputLine.contains('\n'))) {
        for (const auto &line : outputLine.split('\n'))
            addOutputLine(line, channel);
        return;
    }

    m_textOutput->appendMessage(QString::fromUtf8(outputLine) + '\n',
                                channel == OutputChannel::StdOut ? OutputFormat::StdOutFormat
                                                                 : OutputFormat::StdErrFormat);
}

QWidget *TestResultsPane::outputWidget(QWidget *parent)
{
    if (m_outputWidget) {
        m_outputWidget->setParent(parent);
    } else {
        qDebug() << "This should not happen...";
    }
    return m_outputWidget;
}

QStringList TestResultsPane::outputTexts() const
{
    return {m_textOutput->toPlainText()};
}

QList<Core::IOutputPane::ToolBarItem> TestResultsPane::toolBarItems() const
{
    // Seventh of nine, where it was before it stopped being a QToolButton.
    QList<ToolBarItem> items = {ToolBarItem::forWidget(m_expandCollapse),
                                ToolBarItem::forWidget(m_runAll),
                                ToolBarItem::forWidget(m_runSelected),
                                ToolBarItem::forWidget(m_runFailed),
                                ToolBarItem::forWidget(m_runFile),
                                ToolBarItem::forWidget(m_stopTestRun),
                                ToolBarItem::forAspect(
                                    const_cast<Utils::BoolAspect *>(&m_showDuration)),
                                ToolBarItem::forWidget(m_outputToggleButton),
                                ToolBarItem::forWidget(m_filterButton)};
    for (const ToolBarItem &item : baseToolBarItems())
        items << item;
    return items;
}

QList<Utils::BaseAspect *> TestResultsPane::toolBarAspects() const
{
    return {const_cast<Utils::BoolAspect *>(&m_showDuration)};
}

QList<QWidget *> TestResultsPane::toolBarWidgets() const
{
    // The order is in toolBarItems(); this stays for anyone asking what
    // widgets the pane has rather than how its toolbar is laid out.
    return {m_expandCollapse, m_runAll, m_runSelected, m_runFailed,
            m_runFile, m_stopTestRun, m_outputToggleButton, m_filterButton};
}

void TestResultsPane::clearContents()
{
    m_pendingRunner.reset();
    m_bufferTimer.stop();
    m_buffered.clear();
    m_lastCurrentMessage.reset();

    m_filterModel->clearTestResults();
    setIconBadgeNumber(0);
    navigateStateChanged();
    m_summaryWidget->setVisible(false);
    m_autoScroll = testSettings().autoScroll();
    connect(m_model, &QAbstractItemModel::rowsInserted,
            this, &TestResultsPane::onResultsGrew, Qt::UniqueConnection);
    m_textOutput->reset();
    m_textOutput->clear();
    clearMarks();
}

void TestResultsPane::setFocus()
{
    m_outputWidget->setFocus();
}

bool TestResultsPane::hasFocus() const
{
    return m_outputWidget->hasFocus();
}

bool TestResultsPane::canFocus() const
{
    return true;
}

bool TestResultsPane::canNavigate() const
{
    return true;
}

bool TestResultsPane::canNext() const
{
    return m_filterModel->hasResults();
}

bool TestResultsPane::canPrevious() const
{
    return m_filterModel->hasResults();
}

void TestResultsPane::goToNext()
{
    if (!canNext())
        return;

    const QModelIndex currentIndex = m_treeView->currentIndex();
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
        TreeItem *rootItem = m_model->itemForIndex(QModelIndex());
        // if the tree does not contain any item - don't do anything
        if (!rootItem || !rootItem->childCount())
            return;

        nextCurrentIndex = m_filterModel->mapFromSource(m_model->indexForItem(rootItem->childAt(0)));
    }

    m_treeView->setCurrentIndex(nextCurrentIndex);
    onItemActivated(nextCurrentIndex);
}

void TestResultsPane::goToPrev()
{
    if (!canPrevious())
        return;

    const QModelIndex currentIndex = m_treeView->currentIndex();
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

    m_treeView->setCurrentIndex(nextCurrentIndex);
    onItemActivated(nextCurrentIndex);
}

void TestResultsPane::updateFilter()
{
    const bool displaysText = m_outputWidget->currentIndex() == 1;
    if (displaysText) {
        m_textOutput->setFilter(filterText(), filterCaseSensitivity(), filterUsesRegexp(),
                                filterIsInverted(), beforeContext(), afterContext());
    } else {
        m_filterModel->updateFilterProperties(filterText(), filterCaseSensitivity(),
                                              filterUsesRegexp(), filterIsInverted());
        // filtering results in a collapsed tree even if just a leaf node matches
        if (!filterText().isEmpty() || (m_expandCollapse && m_expandCollapse->isChecked()))
            m_treeView->expandAll();
    }
}

void TestResultsPane::onItemActivated(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    const TestResult testResult = m_filterModel->testResult(index);
    if (testResult.isValid() && !testResult.fileName().isEmpty())
        EditorManager::openEditorAt(Link{testResult.fileName(), testResult.line(), 0});
}

void TestResultsPane::initializeFilterMenu()
{
    QMap<ResultType, QString> textAndType;
    textAndType.insert(ResultType::Pass, Tr::tr("Pass"));
    textAndType.insert(ResultType::Fail, Tr::tr("Fail"));
    textAndType.insert(ResultType::ExpectedFail, Tr::tr("Expected Fail"));
    textAndType.insert(ResultType::UnexpectedPass, Tr::tr("Unexpected Pass"));
    textAndType.insert(ResultType::Skip, Tr::tr("Skip"));
    textAndType.insert(ResultType::Benchmark, Tr::tr("Benchmarks"));
    textAndType.insert(ResultType::MessageDebug, Tr::tr("Debug Messages"));
    textAndType.insert(ResultType::MessageWarn, Tr::tr("Warning Messages"));
    textAndType.insert(ResultType::MessageInternal, Tr::tr("Internal Messages"));
    const QSet<ResultType> enabled = m_filterModel->enabledFilters();
    for (auto it = textAndType.cbegin(); it != textAndType.cend(); ++it) {
        const ResultType &result = it.key();
        QAction *action = new QAction(m_filterMenu);
        action->setText(it.value());
        action->setCheckable(true);
        action->setChecked(enabled.contains(result));
        action->setData(int(result));
        m_filterMenu->addAction(action);
    }
    m_filterMenu->addSeparator();
    QAction *action = new QAction(Tr::tr("Check All Filters"), m_filterMenu);
    m_filterMenu->addAction(action);
    connect(action, &QAction::triggered, this, [this] { TestResultsPane::checkAllFilter(true); });
    action = new QAction(Tr::tr("Uncheck All Filters"), m_filterMenu);
    m_filterMenu->addAction(action);
    connect(action, &QAction::triggered, this, [this] { TestResultsPane::checkAllFilter(false); });
}

void TestResultsPane::updateSummaryLabel()
{
    QString labelText = QString("<p>");
    labelText.append(Tr::tr("Test summary"));
    labelText.append(":&nbsp;&nbsp; ");
    int count = m_model->resultTypeCount(ResultType::Pass);
    labelText += QString::number(count) + ' ' + Tr::tr("passes");
    count = m_model->resultTypeCount(ResultType::Fail);
    labelText += ", " + QString::number(count) + ' ' + Tr::tr("fails");
    count = m_model->resultTypeCount(ResultType::UnexpectedPass);
    if (count)
        labelText += ", " + QString::number(count) + ' ' + Tr::tr("unexpected passes");
    count = m_model->resultTypeCount(ResultType::ExpectedFail);
    if (count)
        labelText += ", " + QString::number(count) + ' ' + Tr::tr("expected fails");
    count = m_model->resultTypeCount(ResultType::MessageFatal);
    if (count)
        labelText += ", " + QString::number(count) + ' ' + Tr::tr("fatals");
    count = m_model->resultTypeCount(ResultType::BlacklistedFail)
            + m_model->resultTypeCount(ResultType::BlacklistedXFail)
            + m_model->resultTypeCount(ResultType::BlacklistedPass)
            + m_model->resultTypeCount(ResultType::BlacklistedXPass);
    if (count)
        labelText += ", " + QString::number(count) + ' ' + Tr::tr("blacklisted");
    count = m_model->resultTypeCount(ResultType::Skip);
    if (count)
        labelText += ", " + QString::number(count) + ' ' + Tr::tr("skipped");
    count = m_model->disabledTests();
    if (count)
        labelText += ", " + QString::number(count) + ' ' + Tr::tr("disabled");
    if (auto millisec = m_model->reportedDuration())
        labelText += ".&nbsp;&nbsp;&nbsp;(" + QString::number(*millisec) + " ms)</p>";
    else
        labelText.append(".</p>");
    m_summaryLabel->setText(labelText);
}

void TestResultsPane::checkAllFilter(bool checked)
{
    for (QAction *action : m_filterMenu->actions()) {
        if (action->isCheckable())
            action->setChecked(checked);
    }
    m_filterModel->enableAllResultTypes(checked);
}

void TestResultsPane::filterMenuTriggered(QAction *action)
{
    m_filterModel->toggleTestResultType(TestResult::toResultType(action->data().value<int>()));
    navigateStateChanged();
}

void TestResultsPane::onTestRunStarted()
{
    m_testRunning = true;
    m_stopTestRun->setEnabled(true);
    updateMenuItemsEnabledState();
    m_summaryWidget->setVisible(false);
}

static bool hasFailedTests(const TestResultModel *model)
{
    return (model->resultTypeCount(ResultType::Fail) > 0
            || model->resultTypeCount(ResultType::MessageFatal) > 0
            || model->resultTypeCount(ResultType::UnexpectedPass) > 0);
}

void TestResultsPane::onTestRunFinished()
{
    m_lastCurrentMessage.reset(); // avoid re-adding buffered current message
    m_testRunning = false;
    m_stopTestRun->setEnabled(false);

    updateSummaryLabel();
    m_summaryWidget->setVisible(true);
    m_model->removeCurrentTestMessage();
    disconnect(m_model, &QAbstractItemModel::rowsInserted,
               this, &TestResultsPane::onResultsGrew);
    if (!TestRunner::instance()->suppressPopups() && testSettings().popupOnFinish()
            && (!testSettings().popupOnFail() || hasFailedTests(m_model))) {
        popup(IOutputPane::NoModeSwitch);
    }
    updateMenuItemsEnabledState();
}

void TestResultsPane::onResultsGrew()
{
    // Only while the reader has not moved away from the end: being dragged to
    // the bottom while reading a failure is worse than not following at all.
    if (m_autoScroll && m_atEnd)
        m_treeView->scrollToEnd();
}

void TestResultsPane::onCustomContextMenuRequested(const QModelIndex &index)
{
    const bool resultsAvailable = m_filterModel->hasResults();
    const bool enabled = !m_testRunning && resultsAvailable;
    const TestResult clicked = getTestResult(index);
    QMenu menu;

    QAction *action = new QAction(Tr::tr("Copy"), &menu);
    action->setShortcut(QKeySequence(QKeySequence::Copy));
    action->setEnabled(resultsAvailable && clicked.isValid());
    connect(action, &QAction::triggered, this, [this, clicked] {
       onCopyItemTriggered(clicked);
    });
    menu.addAction(action);

    action = new QAction(Tr::tr("Copy All"), &menu);
    action->setEnabled(enabled);
    connect(action, &QAction::triggered, this, &TestResultsPane::onCopyWholeTriggered);
    menu.addAction(action);

    action = new QAction(Tr::tr("Save Output to File..."), &menu);
    action->setEnabled(enabled);
    connect(action, &QAction::triggered, this, &TestResultsPane::onSaveWholeTriggered);
    menu.addAction(action);

    const auto correlatingItem = (enabled && clicked.isValid()) ? clicked.findTestTreeItem() : nullptr;
    action = new QAction(Tr::tr("Run This Test"), &menu);
    action->setEnabled(correlatingItem && correlatingItem->canProvideTestConfiguration());
    connect(action, &QAction::triggered, this, [this, clicked] {
        onRunThisTestTriggered(TestRunMode::Run, clicked);
    });
    menu.addAction(action);

    action = new QAction(Tr::tr("Run This Test Without Deployment"), &menu);
    action->setEnabled(correlatingItem && correlatingItem->canProvideTestConfiguration());
    connect(action, &QAction::triggered, this, [this, clicked] {
        onRunThisTestTriggered(TestRunMode::RunWithoutDeploy, clicked);
    });
    menu.addAction(action);

    action = new QAction(Tr::tr("Debug This Test"), &menu);
    bool debugEnabled = false;
    if (correlatingItem) {
        if (correlatingItem->testBase()->type() == ITestBase::Framework) {
            auto testTreeItem = static_cast<const TestTreeItem *>(correlatingItem);
            debugEnabled = testTreeItem && testTreeItem->canProvideDebugConfiguration();
        }
    }
    action->setEnabled(debugEnabled);
    connect(action, &QAction::triggered, this, [this, clicked] {
        onRunThisTestTriggered(TestRunMode::Debug, clicked);
    });
    menu.addAction(action);

    action = new QAction(Tr::tr("Debug This Test Without Deployment"), &menu);
    action->setEnabled(debugEnabled);
    connect(action, &QAction::triggered, this, [this, clicked] {
        onRunThisTestTriggered(TestRunMode::DebugWithoutDeploy, clicked);
    });
    menu.addAction(action);

    // Where the pointer is: the view reports which row was clicked, not where
    // on screen, and a menu has to open somewhere.
    menu.exec(QCursor::pos());
}

TestResult TestResultsPane::getTestResult(const QModelIndex &idx)
{
    if (!idx.isValid())
        return {};
    const TestResult result = m_filterModel->testResult(idx);
    QTC_CHECK(result.isValid());
    return result;
}

void TestResultsPane::onCopyItemTriggered(const TestResult &result)
{
    QTC_ASSERT(result.isValid(), return);
    setClipboardAndSelection(result.outputString(true));
}

void TestResultsPane::onCopyWholeTriggered()
{
    setClipboardAndSelection(getWholeOutput());
}

void TestResultsPane::onSaveWholeTriggered()
{
    const FilePath filePath = FileUtils::getSaveFilePath(Tr::tr("Save Output To"));
    if (filePath.isEmpty())
        return;

    FileSaver saver(filePath, QIODevice::Text);
    if (!saver.write(getWholeOutput().toUtf8()) || !saver.finalize()) {
        QMessageBox::critical(ICore::dialogParent(), Tr::tr("Error"),
                              Tr::tr("Failed to write \"%1\".\n\n%2").arg(filePath.toUserOutput())
                              .arg(saver.errorString()));
    }
}

void TestResultsPane::onRunThisTestTriggered(TestRunMode runMode, const TestResult &result)
{
    QTC_ASSERT(result.isValid(), return);

    const ITestTreeItem *item = result.findTestTreeItem();
    if (item)
        TestRunner::instance()->runTest(runMode, item);
}

void TestResultsPane::toggleOutputStyle()
{
    const bool displayText = m_outputWidget->currentIndex() == 0;
    m_outputWidget->setCurrentIndex(displayText ? 1 : 0);
    m_outputToggleButton->setIcon(displayText ? Icons::VISUAL_DISPLAY.icon()
                                              : Icons::TEXT_DISPLAY.icon());
    updateFilter();
    setZoomButtonsEnabled(displayText);
}

// helper for onCopyWholeTriggered() and onSaveWholeTriggered()
QString TestResultsPane::getWholeOutput(const QModelIndex &parent)
{
    QString output;
    for (int row = 0, count = m_model->rowCount(parent); row < count; ++row) {
        QModelIndex current = m_model->index(row, 0, parent);
        const TestResult result = m_model->testResult(current);
        QTC_ASSERT(result.isValid(), continue);
        if (auto item = m_model->itemForIndex(current))
            output.append(item->resultString()).append('\t');
        output.append(result.outputString(true)).append('\n');
        output.append(getWholeOutput(current));
    }
    return output;
}

void TestResultsPane::createMarks(const QModelIndex &parent)
{
    const TestResult parentResult = m_model->testResult(parent);
    const ResultType parentType = parentResult.isValid() ? parentResult.result() : ResultType::Invalid;
    const QList<ResultType> interested{ResultType::Fail, ResultType::UnexpectedPass};
    for (int row = 0, count = m_model->rowCount(parent); row < count; ++row) {
        const QModelIndex index = m_model->index(row, 0, parent);
        const TestResult result = m_model->testResult(index);
        QTC_ASSERT(result.isValid(), continue);

        if (m_model->hasChildren(index))
            createMarks(index);

        bool isLocationItem = result.result() == ResultType::MessageLocation;
        if (interested.contains(result.result())
                || (isLocationItem && interested.contains(parentType))) {
            // do not pollute with too many marks - they won't be readable at all
            if (m_marks.values({result.fileName(), result.line()}).size() > 13)
                continue;

            TestEditorMark *mark = new TestEditorMark(index, result.fileName(), result.line());
            mark->setIcon(Icons::TEXTMARK_FAIL.icon());
            mark->setColor(Theme::OutputPanes_TestFailTextColor);
            mark->setPriority(TextEditor::TextMark::NormalPriority);
            mark->setToolTip(result.description());
            m_marks.insert({result.fileName(), result.line()}, mark);
        }
    }
}

void TestResultsPane::clearMarks()
{
    qDeleteAll(m_marks);
    m_marks.clear();
}

static constexpr char SV_SHOW_DURATIONS[] = "AutoTest.ShowDurations";
static constexpr char SV_MESSAGE_FILTER[] = "AutoTest.MessageFilter";

void TestResultsPane::onSessionLoaded()
{
    const bool showDurations = SessionManager::sessionValue(SV_SHOW_DURATIONS, true).toBool();
    m_showDuration.setValue(showDurations);
    const QVariantList enabledFilters = SessionManager::sessionValue(SV_MESSAGE_FILTER).toList();

    if (enabledFilters.isEmpty()) {
        m_filterModel->enableAllResultTypes(true);
        if (testSettings().omitInternalMsg())
            m_filterModel->toggleTestResultType(ResultType::MessageInternal);
    } else {
        m_filterModel->setEnabledFiltersFromSetting(enabledFilters);
    }

    m_filterMenu->clear();
    initializeFilterMenu();
}

void TestResultsPane::onAboutToSaveSession()
{
    SessionManager::setSessionValue(SV_SHOW_DURATIONS, m_showDuration());
    SessionManager::setSessionValue(SV_MESSAGE_FILTER, m_filterModel->enabledFiltersAsSetting());
}

void TestResultsPane::showTestResult(const QModelIndex &index)
{
    QModelIndex mapped = m_filterModel->mapFromSource(index);
    if (mapped.isValid()) {
        popup(IOutputPane::NoModeSwitch);
        m_treeView->setCurrentIndex(mapped);
    }
}

bool TestResultsPane::expandIntermediate() const
{
    return !m_pendingRunner.isRunning() && m_expandCollapse->isChecked();
}

void TestResultsPane::aboutToShutdown()
{
    m_pendingRunner.cancel();
}

struct ExpandedRows
{
    int row;
    QList<ExpandedRows> childRows;
};

// Which rows are open, and putting them back. Asked of whatever can answer for
// a view rather than of a view: the model under this one is replaced wholesale
// when buffered results are merged in, and a reader who had opened a failure to
// read it should still be looking at it afterwards.
//
// A widget tree view answers these directly; a Qt Quick one answers through the
// pane holding it, which is the only thing that knows how a tree is laid out in
// rows.
QList<ExpandedRows> collectExpanded(const QAbstractItemModel *model,
                                    const std::function<bool(const QModelIndex &)> &isExpanded,
                                    const QModelIndex &parent)
{
    QList<ExpandedRows> result;
    const int rowsEnd = model->rowCount(parent);
    for (int row = 0; row < rowsEnd; ++row) {
        const QModelIndex child = model->index(row, 0, parent);
        if (isExpanded(child))
            result.append({row, collectExpanded(model, isExpanded, child)});
    }
    return result;
}

void reexpand(const QAbstractItemModel *model,
              const std::function<void(const QModelIndex &)> &expand,
              const QList<ExpandedRows> &expanded, const QModelIndex &parent)
{
    for (const ExpandedRows &exp : expanded) {
        const QModelIndex child = model->index(exp.row, 0, parent);
        expand(child);
        reexpand(model, expand, exp.childRows, child);
    }
}

// Where an index is, as the rows to walk from the root - which survives the
// model being replaced, as an index does not.
QList<int> rowPath(const QModelIndex &index)
{
    QList<int> path;
    for (QModelIndex idx = index; idx.isValid(); idx = idx.parent())
        path.prepend(idx.row());
    return path;
}

// And back again, in whatever model is there now. Invalid where the tree no
// longer goes that deep: what the reader had selected may simply not be there
// any more.
QModelIndex indexAtPath(const QAbstractItemModel *model, const QList<int> &path)
{
    QModelIndex index;
    for (const int row : path) {
        if (row < 0 || row >= model->rowCount(index))
            return {};
        index = model->index(row, 0, index);
    }
    return index;
}

using CopyAndAddResult = Result<std::unique_ptr<TestResultItem>>;

static void copyAndAddPending(QPromise<CopyAndAddResult> &promise,
                              const QList<TestResult> &original,
                              const QList<TestResult> &buffered)
{
    std::unique_ptr<TestResultItem> newRoot = std::make_unique<TestResultItem>(TestResult{});
    for (const TestResult &result : original) {
        if (promise.isCanceled())
            return;
        newRoot->addTestResult(result, false);
    }
    for (const TestResult &result : buffered) {
        if (promise.isCanceled())
            return;
        newRoot->addTestResult(result, false);
    }
    promise.addResult(std::move(newRoot));
}

void TestResultsPane::handlePendingResultsSilently()
{
    auto onSetup = [this](Async<CopyAndAddResult> &task) {
        QList<TestResult> originalResults;
        m_model->rootItem()->forAllChildren([&originalResults](TreeItem *it) {
            originalResults.append(static_cast<TestResultItem *>(it)->testResult());
        });
        task.setConcurrentCallData(&copyAndAddPending, originalResults, m_buffered);
        m_buffered.clear();
        task.setFutureSynchronizer(nullptr);
    };
    auto onDone = [this](const Async<CopyAndAddResult> &async) {
        if (!async.isResultAvailable()) // task was canceled, no result at all
            return;
        CopyAndAddResult newRoot = async.takeResult();
        if (!newRoot) // no result? so keep whatever is present already
            return;

        // collect current model's expansion, selection and position
        const auto isExpanded = [this](const QModelIndex &index) {
            return m_treeView->isExpanded(index);
        };
        const QList<ExpandedRows> origExpanded = collectExpanded(m_filterModel, isExpanded, {});
        QList<int> selectedRows;
        selectedRows = rowPath(m_treeView->currentIndex());
        const bool wasAtEnd = m_treeView->isAtEnd();

        // exchange root items
        m_model->setRootItem(newRoot->release());

        // restore expansion, selection and position, ignore expansion of items added silently
        const auto expand = [this](const QModelIndex &index) { m_treeView->expand(index); };
        reexpand(m_filterModel, expand, origExpanded, {});
        if (!selectedRows.isEmpty()) {
            const QModelIndex idx = indexAtPath(m_filterModel, selectedRows);
            if (idx.isValid())
                m_treeView->setCurrentIndex(idx);
        }
        if (wasAtEnd)
            m_treeView->scrollToEnd();
        createMarks();
        updateMenuItemsEnabledState();
    };

    m_pendingRunner.start({AsyncTask<CopyAndAddResult>{onSetup, onDone}});
}

#ifdef WITH_TESTS

class TestResultsTreeStateTest final : public QObject
{
    Q_OBJECT

private:
    // Three parents, each with two children.
    static void fill(QStandardItemModel &model)
    {
        for (int i = 0; i < 3; ++i) {
            auto * const parent = new QStandardItem(QString("parent %1").arg(i));
            for (int j = 0; j < 2; ++j)
                parent->appendRow(new QStandardItem(QString("child %1.%2").arg(i).arg(j)));
            model.appendRow(parent);
        }
    }

private slots:
    void testTheViewAnswersForItsRowsWithoutBeingOne()
    {
        // Everything the pane used to ask a QTreeView. Which row an index is
        // on depends on what is open above it, so only the view can say - and
        // the pane needs the answer to put a reader back where they were.
        QStandardItemModel model;
        fill(model);

        TestResultsView view(&model);
        view.resize(400, 300);
        view.show();

        const QModelIndex first = model.index(0, 0);
        const QModelIndex third = model.index(2, 0);
        QTRY_VERIFY2(!view.isExpanded(first), "a row was open before anything opened it");

        view.expand(first);
        QTRY_VERIFY2(view.isExpanded(first), "opening a row did not open it");
        QVERIFY2(!view.isExpanded(third), "opening one row opened another");

        // The current row is the pane's, and comes back as the index that was
        // put in rather than as a row number.
        QVERIFY(!view.currentIndex().isValid());
        view.setCurrentIndex(third);
        QCOMPARE(view.currentIndex(), third);

        // All of them, and none of them.
        view.expandAll();
        QTRY_VERIFY(view.isExpanded(third));
        view.collapseAll();
        QTRY_VERIFY2(!view.isExpanded(first), "collapsing all left a row open");
    }

    void testRememberingWhichRowsWereOpen()
    {
        QStandardItemModel model;
        fill(model);

        // Nothing open: nothing to put back.
        const auto nothingOpen = [](const QModelIndex &) { return false; };
        QVERIFY(collectExpanded(&model, nothingOpen, {}).isEmpty());

        // The first parent open, and the second child of the third.
        const QModelIndex first = model.index(0, 0);
        const QModelIndex thirdChild = model.index(1, 0, model.index(2, 0));
        const auto someOpen = [&](const QModelIndex &index) {
            return index == first || index == model.index(2, 0) || index == thirdChild;
        };

        const QList<ExpandedRows> open = collectExpanded(&model, someOpen, {});
        QCOMPARE(open.size(), 2);
        QCOMPARE(open.first().row, 0);
        QVERIFY2(open.first().childRows.isEmpty(),
                 "a closed child was remembered as open");
        QCOMPARE(open.last().row, 2);
        QCOMPARE(open.last().childRows.size(), 1);
        QCOMPARE(open.last().childRows.first().row, 1);

        // And putting it back opens exactly those again - in a *different*
        // model, which is the point: the one it was read from is replaced.
        QStandardItemModel replacement;
        fill(replacement);

        QList<QModelIndex> opened;
        reexpand(&replacement, [&](const QModelIndex &index) { opened << index; }, open, {});
        QCOMPARE(opened.size(), 3);
        QCOMPARE(opened.at(0), replacement.index(0, 0));
        QCOMPARE(opened.at(1), replacement.index(2, 0));
        QCOMPARE(opened.at(2), replacement.index(1, 0, replacement.index(2, 0)));
    }

    void testFindingARowAgainAfterTheModelIsReplaced()
    {
        QStandardItemModel model;
        fill(model);

        const QModelIndex child = model.index(1, 0, model.index(2, 0));
        QCOMPARE(rowPath(child), QList<int>({2, 1}));
        QCOMPARE(rowPath({}), QList<int>());

        QStandardItemModel replacement;
        fill(replacement);
        QCOMPARE(indexAtPath(&replacement, {2, 1}), replacement.index(1, 0, replacement.index(2, 0)));

        // A path the new tree does not go down is not a row: what the reader
        // had selected may simply not be there any more, and answering with
        // some other row would put them somewhere they never chose.
        QVERIFY(!indexAtPath(&replacement, {2, 9}).isValid());
        QVERIFY(!indexAtPath(&replacement, {7}).isValid());
        QVERIFY(!indexAtPath(&replacement, {2, 1, 0}).isValid());

        // And it stops at the first step it cannot take. Walking on from an
        // invalid index starts again at the top of the tree, so a path that
        // runs off the end would come back as some unrelated row - and the
        // reader would be moved somewhere they never chose.
        QVERIFY2(!indexAtPath(&replacement, {2, 9, 0}).isValid(),
                 "a path that ran off the tree came back as a row near the top");
    }
};

QObject *createTestResultsTreeStateTest()
{
    return new TestResultsTreeStateTest;
}

#endif // WITH_TESTS

} // namespace Autotest::Internal

#ifdef WITH_TESTS
#include "testresultspane.moc"
#endif

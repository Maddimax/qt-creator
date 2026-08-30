// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "testresultmodel.h"

#include <utils/theme/theme.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include "autotesticons.h"
#include "testresultspane.h"
#include "testrunner.h"
#include "testsettings.h"
#include "testtreeitem.h"
#include "testtreemodel.h"

#include <projectexplorer/projectexplorericons.h>
#include <utils/algorithm.h>
#include <utils/qtcassert.h>
#include <utils/themedvalue.h>

#include <QFontMetrics>
#include <QIcon>
#include <QToolButton>

using namespace Utils;

namespace Autotest::Internal {

/********************************* TestResultItem ******************************************/

TestResultItem::TestResultItem(const TestResult &testResult)
    : m_testResult(testResult)
{
}

static QIcon testResultIcon(ResultType result) {
    static const Utils::ThemedValue<QList<QIcon>> icons([] {
        return QList<QIcon>{
            Icons::RESULT_PASS.icon(),
            Icons::RESULT_FAIL.icon(),
            Icons::RESULT_XFAIL.icon(),
            Icons::RESULT_XPASS.icon(),
            Icons::RESULT_SKIP.icon(),
            Icons::RESULT_BLACKLISTEDPASS.icon(),
            Icons::RESULT_BLACKLISTEDFAIL.icon(),
            Icons::RESULT_BLACKLISTEDXPASS.icon(),
            Icons::RESULT_BLACKLISTEDXFAIL.icon(),
            Icons::RESULT_BENCHMARK.icon(),
            Icons::RESULT_MESSAGEDEBUG.icon(),
            Icons::RESULT_MESSAGEDEBUG.icon(), // Info gets the same handling as Debug for now
            Icons::RESULT_MESSAGEWARN.icon(),
            Icons::RESULT_MESSAGEFATAL.icon(),
            Icons::RESULT_MESSAGEFATAL.icon(), // System gets same handling as Fatal for now
            Icons::RESULT_MESSAGEFATAL.icon(), // Error gets same handling as Fatal for now
            ProjectExplorer::Icons::DESKTOP_DEVICE.icon(),  // for now
        }; // provide an icon for unknown??
    });

    if (result < ResultType::FIRST_TYPE || result >= ResultType::MessageInternal) {
        switch (result) {
        case ResultType::Application:
            return icons().at(16);
        default:
            return QIcon();
        }
    }
    return icons().at(int(result));
}

static QIcon testSummaryIcon(const std::optional<TestResultItem::SummaryEvaluation> &summary)
{
    if (!summary)
        return QIcon();
    if (summary->failed)
        return summary->warnings ? Icons::RESULT_MESSAGEFAILWARN.icon() : Icons::RESULT_FAIL.icon();
    return summary->warnings ? Icons::RESULT_MESSAGEPASSWARN.icon() : Icons::RESULT_PASS.icon();
}

QVariant TestResultItem::data(int column, int role) const
{
    switch (role) {
    case Qt::DecorationRole: {
        if (!m_testResult.isValid())
            return {};
        const ResultType result = m_testResult.result();
        if (result == ResultType::MessageLocation && parent())
            return parent()->data(column, role);
        if (result == ResultType::TestStart)
            return testSummaryIcon(m_summaryResult);
        return testResultIcon(result);
    }
    case Qt::DisplayRole:
        return m_testResult.isValid() ? m_testResult.outputString(true) : QVariant();
    case ResultStringRole:
        return resultString();
    case ResultColorRole:
        if (!m_testResult.isValid())
            return {};
        // A row that only says a test started is not a result, so it is not
        // written in a result's colour.
        if (m_testResult.result() == ResultType::TestStart)
            return Utils::creatorColor(Utils::Theme::TextColorDisabled);
        return TestResult::colorForType(m_testResult.result());
    case SummaryRole:
        return m_testResult.isValid() ? m_testResult.outputString(false) : QVariant();
    case FullOutputRole:
        return m_testResult.isValid() ? m_testResult.outputString(true) : QVariant();
    case DurationRole:
        if (!m_testResult.isValid() || !m_testResult.duration())
            return {};
        return *m_testResult.duration();
    case FileNameRole:
        return m_testResult.isValid() ? m_testResult.fileName().fileName() : QVariant();
    case LineRole:
        return m_testResult.isValid() && m_testResult.line() ? m_testResult.line() : QVariant();
    default:
        return TreeItem::data(column, role);
    }
}

void TestResultItem::updateDescription(const QString &description)
{
    QTC_ASSERT(m_testResult.isValid(), return);
    m_testResult.setDescription(description);
}

static bool isSignificant(ResultType type)
{
    switch (type) {
    case ResultType::Benchmark:
    case ResultType::MessageInfo:
    case ResultType::MessageInternal:
    case ResultType::TestEnd:
        return false;
    case ResultType::MessageLocation:
    case ResultType::MessageCurrentTest:
    case ResultType::Application:
    case ResultType::Invalid:
        QTC_ASSERT_STRING("Got unexpected type in isSignificant check");
        return false;
    default:
        return true;
    }
}

static bool isFailed(ResultType type)
{
    switch (type) {
    case ResultType::Fail: case ResultType::UnexpectedPass: case ResultType::MessageFatal:
        return true;
    default:
        return false;
    }
}

static void updateParentOf(const TestResultItem *item)
{
    QTC_ASSERT(item, return);
    TestResultItem *parentItem = item->parent();
    if (parentItem == nullptr) // do not update invisible root item
        return;
    QTC_ASSERT(item->testResult().isValid(), return);
    bool changed = false;
    parentItem->updateResult(changed, item->testResult().result(), item->summaryResult(),
                             item->testResult().duration());
    bool changedType = parentItem->updateDescendantTypes(item->testResult().result());
    if (!changed && !changedType)
        return;
    if (item->model())
        emit item->model()->dataChanged(parentItem->index(), parentItem->index());
    updateParentOf(parentItem);
}

static TestResultItem *findDirectParent(TreeItem *it, const std::function<bool (TreeItem *)> pred)
{
    if (auto lastChild = it->lastChild()) {
        if (TestResultItem *found = findDirectParent(lastChild, pred))
            return found;

        if (pred(lastChild))
            return static_cast<TestResultItem *>(lastChild);
    }
    return nullptr;
}

static TestResultItem *findParentItemFor(const TestResultItem *item,
                                         const TestResultItem *startItem,
                                         const TestResultItem *rootItem)
{
    QTC_ASSERT(item, return nullptr);
    TestResultItem *root = startItem ? const_cast<TestResultItem *>(startItem) : nullptr;
    const TestResult result = item->testResult();
    const QString &name = result.name();
    const QString &id = result.id();

    if (root == nullptr && !name.isEmpty()) {
        for (int row = rootItem->childCount() - 1; row >= 0; --row) {
            TestResultItem *tmp = rootItem->childAt(row);
            const TestResult tmpTestResult = tmp->testResult();
            if (tmpTestResult.id() == id && tmpTestResult.name() == name) {
                root = tmp;
                break;
            }
        }
    }
    if (root == nullptr)
        return root;

    bool needsIntermediate = false;
    auto predicate = [result, &needsIntermediate](TreeItem *it) {
        TestResultItem *currentItem = static_cast<TestResultItem *>(it);
        return currentItem->testResult().isDirectParentOf(result, &needsIntermediate);
    };

    if (TestResultItem *parent = findDirectParent(root, predicate)) {
        if (needsIntermediate) {
            // check if the intermediate is present already
            if (TestResultItem *intermediate = parent->intermediateFor(item))
                return intermediate;
            return parent->createAndAddIntermediateFor(item);
        }
        return parent;
    }
    return root;
}

// call this function on the root item of the model!
void TestResultItem::addTestResult(const TestResult &testResult, bool autoExpand)
{
    QTC_ASSERT(!parent(), return);

    const int lastRow = childCount() - 1;
    if (testResult.result() == ResultType::MessageCurrentTest) {
        // MessageCurrentTest should always be the last top level item
        if (lastRow >= 0) {
            TestResultItem *current = childAt(lastRow);
            const TestResult result = current->testResult();
            if (result.isValid() && result.result() == ResultType::MessageCurrentTest) {
                current->updateDescription(testResult.description());
                if (model())
                   emit model()->dataChanged(current->index(), current->index());
                return;
            }
        }

        appendChild(new TestResultItem(testResult));
        return;
    }

    TestResultItem *newItem = new TestResultItem(testResult);
    TestResultItem *root = nullptr;
    if (testSettings().displayApplication()) {
        const QString application = testResult.id();
        if (!application.isEmpty()) {
            root = findFirstLevelChild([&application](TestResultItem *child) {
                QTC_ASSERT(child, return false);
                return child->testResult().id() == application;
            });

            if (!root) {
                TestResult tmpAppResult(application, application);
                tmpAppResult.setResult(ResultType::Application);
                root = new TestResultItem(tmpAppResult);
                if (lastRow >= 0)
                    insertChild(lastRow, root);
                else
                    appendChild(root);
            }
        }
    }

    TestResultItem *parentItem = findParentItemFor(newItem, root, this);
    if (parentItem) {
        parentItem->appendChild(newItem);
        if (autoExpand && parentItem->model()) {
            QMetaObject::invokeMethod(parentItem->model(), [parentItem]{ parentItem->expand(); },
                                      Qt::QueuedConnection);
        }
        updateParentOf(newItem);
    } else {
        if (lastRow >= 0) {
            TestResultItem *current = childAt(lastRow);
            const TestResult result = current->testResult();
            if (result.isValid() && result.result() == ResultType::MessageCurrentTest) {
                insertChild(current->index().row(), newItem);
                return;
            }
        }
        // there is no MessageCurrentTest at the last row, but we have a toplevel item - just add it
        appendChild(newItem);
    }

    if (isFailed(testResult.result())) {
        if (const ITestTreeItem *it = testResult.findTestTreeItem()) {
            TestTreeModel *model = TestTreeModel::instance();
            model->setData(model->indexForItem(it), true, FailedRole);
        }
    }
}

void TestResultItem::updateResult(bool &changed, ResultType addedChildType,
                                  const std::optional<SummaryEvaluation> &summary,
                                  const std::optional<QString> duration)
{
    changed = false;
    if (m_testResult.result() != ResultType::TestStart)
        return;

    if (addedChildType == ResultType::TestEnd && duration)
        m_testResult.setDuration(*duration);

    if (!isSignificant(addedChildType) || (addedChildType == ResultType::TestStart && !summary))
        return;

    if (m_summaryResult.has_value() && m_summaryResult->failed && m_summaryResult->warnings)
        return; // can't become worse

    SummaryEvaluation newResult = m_summaryResult.value_or(SummaryEvaluation());
    switch (addedChildType) {
    case ResultType::Fail:
    case ResultType::MessageFatal:
    case ResultType::UnexpectedPass:
        if (newResult.failed)
            return;
        newResult.failed = true;
        break;
    case ResultType::ExpectedFail:
    case ResultType::MessageWarn:
    case ResultType::MessageError:
    case ResultType::MessageSystem:
    case ResultType::Skip:
    case ResultType::BlacklistedFail:
    case ResultType::BlacklistedPass:
    case ResultType::BlacklistedXFail:
    case ResultType::BlacklistedXPass:
        if (newResult.warnings)
            return;
        newResult.warnings = true;
        break;
    case ResultType::TestStart:
        if (summary) {
            newResult.failed |= summary->failed;
            newResult.warnings |= summary->warnings;
        }
        break;
    default:
        break;
    }
    changed = !m_summaryResult.has_value() || *m_summaryResult != newResult;

    if (changed)
        m_summaryResult.emplace(newResult);
}

TestResultItem *TestResultItem::intermediateFor(const TestResultItem *item) const
{
    QTC_ASSERT(item, return nullptr);
    if (!hasChildren())
        return nullptr;
    TestResultItem *child = static_cast<TestResultItem *>(lastChild());
    const TestResult testResult = child->testResult();
    if (testResult.result() != ResultType::TestStart)
        return nullptr;
    const TestResult otherResult = item->testResult();
    return testResult.isIntermediateFor(otherResult) ? child : nullptr;
}

TestResultItem *TestResultItem::createAndAddIntermediateFor(const TestResultItem *child)
{
    TestResult result = child->testResult().intermediateResult();
    QTC_ASSERT(result.isValid(), return nullptr);
    result.setResult(ResultType::TestStart);
    TestResultItem *intermediate = new TestResultItem(result);
    appendChild(intermediate);
    if (TestResultsPane::instance()->expandIntermediate()) {
        QMetaObject::invokeMethod(TestResultsPane::instance(),
                                  [intermediate] { intermediate->expand(); },
                                  Qt::QueuedConnection);
    }
    return intermediate;
}

QString TestResultItem::resultString() const
{
    if (testResult().result() != ResultType::TestStart)
        return TestResult::resultToString(testResult().result());
    if (!m_summaryResult)
        return {};
    return m_summaryResult->failed ? QString("FAIL") : QString("PASS");
}

//! \return true if descendant types have changed, false otherwise
bool TestResultItem::updateDescendantTypes(ResultType t)
{
    if (t == ResultType::TestStart || t == ResultType::TestEnd) // these are special
        return false;

    return Utils::insert(m_descendantsTypes, t);
}

bool TestResultItem::descendantTypesContainsAnyOf(const QSet<ResultType> &types) const
{
    return !m_descendantsTypes.isEmpty() && m_descendantsTypes.intersects(types);
}

/********************************* TestResultModel *****************************************/

TestResultModel::TestResultModel(QObject *parent)
    : TreeModel<TestResultItem>(new TestResultItem({}), parent)
{
    connect(TestRunner::instance(), &TestRunner::reportSummary,
            this, [this](const QString &id, const QHash<ResultType, int> &summary){
        m_reportedSummary.insert(id, summary);
    });
    connect(TestRunner::instance(), &TestRunner::reportDuration,
            this, [this](int duration){
        m_reportedDurations.emplace(m_reportedDurations.value_or(0) + duration);
    });
}

void TestResultModel::setRootItem(TestResultItem *root)
{
    BaseTreeModel::setRootItem(root);
}

void TestResultModel::raiseTestResultCount(const QString &id, ResultType type)
{
    m_testResultCount[id][type]++;
}

QHash<int, QByteArray> TestResultModel::roleNames() const
{
    // The base already names display and decoration; the rest are this
    // model's own. AspectTable::withRoleNames() adds the table roles, which
    // nothing here reads - Squish needs it because its rows are coloured
    // through Qt::ForegroundRole, and these carry their own colour instead.
    QHash<int, QByteArray> names = TreeModel::roleNames();
    names.insert(TestResultItem::ResultStringRole, "resultString");
    names.insert(TestResultItem::ResultColorRole, "resultColor");
    names.insert(TestResultItem::SummaryRole, "summary");
    names.insert(TestResultItem::FullOutputRole, "fullOutput");
    names.insert(TestResultItem::DurationRole, "duration");
    names.insert(TestResultItem::FileNameRole, "fileName");
    names.insert(TestResultItem::LineRole, "line");
    return names;
}

void TestResultModel::addTestResult(const TestResult &testResult, bool autoExpand)
{
    if (const QString fn = testResult.fileName().fileName(); !fn.isEmpty())
        addFileName(fn); // ensure we calculate the results pane correctly
    rootItem()->addTestResult(testResult, autoExpand);
}

void TestResultModel::removeCurrentTestMessage()
{
    TestResultItem *currentMessageItem = rootItem()->findFirstLevelChild([](TestResultItem *it) {
            return (it->testResult().result() == ResultType::MessageCurrentTest);
    });
    if (currentMessageItem)
        destroyItem(currentMessageItem);
}

void TestResultModel::clearTestResults()
{
    clear();
    m_testResultCount.clear();
    m_reportedSummary.clear();
    m_reportedDurations.reset();
    m_disabled = 0;
    m_fileNames.clear();
    m_maxWidthOfFileName = 0;
    m_widthOfLineNumber = 0;
}

TestResult TestResultModel::testResult(const QModelIndex &idx)
{
    if (idx.isValid())
        return itemForIndex(idx)->testResult();
    return {};
}

void TestResultModel::recalculateMaxWidthOfFileName(const QFont &font)
{
    const QFontMetrics fm(font);
    m_maxWidthOfFileName = 0;
    for (const QString &fileName : std::as_const(m_fileNames)) {
        m_maxWidthOfFileName = qMax(m_maxWidthOfFileName, fm.horizontalAdvance(fileName));
    }
}

void TestResultModel::addFileName(const QString &fileName)
{
    const QFontMetrics fm(m_measurementFont);
    m_maxWidthOfFileName = qMax(m_maxWidthOfFileName, fm.horizontalAdvance(fileName));
    m_fileNames.insert(fileName);
}

int TestResultModel::maxWidthOfFileName(const QFont &font)
{
    if (font != m_measurementFont)
        recalculateMaxWidthOfFileName(font);
    return m_maxWidthOfFileName;
}

int TestResultModel::maxWidthOfLineNumber(const QFont &font)
{
    if (m_widthOfLineNumber == 0 || font != m_measurementFont) {
        QFontMetrics fm(font);
        m_measurementFont = font;
        m_widthOfLineNumber = fm.horizontalAdvance("88888");
    }
    return m_widthOfLineNumber;
}

int TestResultModel::resultTypeCount(ResultType type) const
{
    int result = 0;
    for (auto it = m_testResultCount.cbegin(); it != m_testResultCount.cend(); ++it) {
        // if we got a result count from the framework prefer that over our counted results
        int reported = m_reportedSummary[it.key()].value(type);
        result += reported != 0 ? reported : it.value().value(type);
    }
    return result;
}

/********************************** Filter Model **********************************/

TestResultFilterModel::TestResultFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    enableAllResultTypes(true);
    if (!testSettings().omitInternalMsg())
        toggleTestResultType(ResultType::MessageInternal);
}

void TestResultFilterModel::enableAllResultTypes(bool enabled)
{
    if (enabled) {
        m_enabled << ResultType::Pass << ResultType::Fail << ResultType::ExpectedFail
                  << ResultType::UnexpectedPass << ResultType::Skip << ResultType::MessageDebug
                  << ResultType::MessageWarn << ResultType::MessageInternal << ResultType::MessageLocation
                  << ResultType::MessageFatal << ResultType::Invalid << ResultType::BlacklistedPass
                  << ResultType::BlacklistedFail << ResultType::BlacklistedXFail << ResultType::BlacklistedXPass
                  << ResultType::Benchmark
                  << ResultType::MessageCurrentTest
                  << ResultType::MessageInfo << ResultType::MessageSystem << ResultType::Application
                  << ResultType::MessageError;
    } else {
        m_enabled.clear();
        m_enabled << ResultType::MessageFatal << ResultType::MessageSystem
                  << ResultType::MessageError;
    }
    invalidateFilter();
}

void TestResultFilterModel::toggleTestResultType(ResultType type)
{
    if (m_enabled.remove(type)) {
        if (type == ResultType::MessageInternal)
            m_enabled.remove(ResultType::TestEnd);
        if (type == ResultType::MessageDebug)
            m_enabled.remove(ResultType::MessageInfo);
        if (type == ResultType::MessageWarn)
            m_enabled.remove(ResultType::MessageSystem);
    } else {
        m_enabled.insert(type);
        if (type == ResultType::MessageInternal)
            m_enabled.insert(ResultType::TestEnd);
        if (type == ResultType::MessageDebug)
            m_enabled.insert(ResultType::MessageInfo);
        if (type == ResultType::MessageWarn)
            m_enabled.insert(ResultType::MessageSystem);
    }
    invalidateFilter();
}

void TestResultFilterModel::clearTestResults()
{
    m_sourceModel->clearTestResults();
}

bool TestResultFilterModel::hasResults()
{
    return rowCount(QModelIndex());
}

TestResult TestResultFilterModel::testResult(const QModelIndex &index) const
{
    return m_sourceModel->testResult(mapToSource(index));
}

TestResultItem *TestResultFilterModel::itemForIndex(const QModelIndex &index) const
{
    return index.isValid() ? m_sourceModel->itemForIndex(mapToSource(index)) : nullptr;
}

const QVariantList TestResultFilterModel::enabledFiltersAsSetting() const
{
    return Utils::transform(Utils::toList(m_enabled),
                            [](ResultType rt) { return QVariant::fromValue(int(rt)); });
}

void TestResultFilterModel::setEnabledFiltersFromSetting(const QVariantList &enabled)
{
    m_enabled.clear();
    if (!enabled.isEmpty()) {
        for (const QVariant &variant : enabled)
            m_enabled << ResultType(variant.value<int>());
    }
    // when misused: ensure non-discardable filters are enabled
    m_enabled << ResultType::MessageFatal << ResultType::MessageSystem << ResultType::MessageError;
    invalidateFilter();
}

void TestResultFilterModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    m_sourceModel = static_cast<TestResultModel *>(sourceModel);
    QSortFilterProxyModel::setSourceModel(sourceModel);
}

void TestResultFilterModel::updateFilterProperties(const QString &filterText,
                                                   Qt::CaseSensitivity caseSensitivity,
                                                   bool isRegexp, bool isInverted)
{
    m_filterText = filterText;
    m_caseSensitivity = caseSensitivity;
    m_regex = isRegexp;
    m_inverted = isInverted;
    if (m_regex) {
        const QRegularExpression::PatternOptions options  = m_caseSensitivity == Qt::CaseSensitive
                ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
        m_filterRegex = QRegularExpression{m_filterText, options};
    }
    invalidateFilter();
}

bool TestResultFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex index = m_sourceModel->index(sourceRow, 0, sourceParent);
    if (!index.isValid())
        return false;

    // filter by type
    const TestResultItem *item = m_sourceModel->itemForIndex(index);
    const TestResult result = item->testResult();
    const ResultType resultType = result.result();
    auto descendentContainsEnabledType = [this](const TestResultItem *item) {
        return item && item->descendantTypesContainsAnyOf(m_enabled);
    };
    if (resultType == ResultType::TestStart) {
        if (!descendentContainsEnabledType(item))
            return false;
    } else if (resultType == ResultType::TestEnd) {
        if (!item || !descendentContainsEnabledType(item->parent()))
            return false;
    } else if (!m_enabled.contains(resultType)) {
        return false;
    }

    // if not filtered out already perform additional filtering by the filter line edit
    if (m_filterText.isEmpty())
        return true;

    const QString text = result.outputString(true);
    if (m_regex)
        return m_filterRegex.isValid() && m_filterRegex.match(text).hasMatch() != m_inverted;
    return text.contains(m_filterText, m_caseSensitivity) != m_inverted;
}

#ifdef WITH_TESTS

class TestResultModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheRowsAreReadableByName()
    {
        // The widget delegate read every part of a row off the TestResult
        // itself, so none of it had a name. A Qt Quick delegate can only ask
        // by name, and a role it asks for under a name nobody answers is
        // undefined rather than an error - the row draws blank and nothing
        // reports it.
        TestResultModel model;
        const QHash<int, QByteArray> names = model.roleNames();

        // Qt's own two. Renaming display is the trap: the stock delegates
        // bind their label to it.
        QCOMPARE(names.value(Qt::DisplayRole), QByteArray("display"));
        QCOMPARE(names.value(Qt::DecorationRole), QByteArray("decoration"));

        QCOMPARE(names.value(TestResultItem::ResultStringRole), QByteArray("resultString"));
        QCOMPARE(names.value(TestResultItem::ResultColorRole), QByteArray("resultColor"));
        QCOMPARE(names.value(TestResultItem::SummaryRole), QByteArray("summary"));
        QCOMPARE(names.value(TestResultItem::FullOutputRole), QByteArray("fullOutput"));
        QCOMPARE(names.value(TestResultItem::DurationRole), QByteArray("duration"));
        QCOMPARE(names.value(TestResultItem::FileNameRole), QByteArray("fileName"));
        QCOMPARE(names.value(TestResultItem::LineRole), QByteArray("line"));

        // And they are answered, not merely named. A role with a name and no
        // value behind it is the same blank row, reached a different way.
        TestResult result("someId", "someTest");
        result.setResult(ResultType::Fail);
        const TestResultItem item(result);
        QVERIFY2(!item.data(0, TestResultItem::ResultStringRole).toString().isEmpty(),
                 "the row names a result string and answers nothing under it");
    }

    void testARowAtRestSaysLessThanTheOneBeingRead()
    {
        // The whole reason the widget delegate was 287 lines: a row shows one
        // line until it is selected, and then it wraps and grows to show
        // everything. Two roles, because a view cannot ask one role two ways.
        // Given an id and a name: a result without them is not valid, and an
        // invalid one answers nothing at all.
        TestResult result("someId", "someTest");
        result.setDescription("first line\nsecond line\nthird line");
        TestResultItem item(result);

        QCOMPARE(item.data(0, TestResultItem::SummaryRole).toString(), QString("first line"));
        QCOMPARE(item.data(0, TestResultItem::FullOutputRole).toString(),
                 QString("first line\nsecond line\nthird line"));
    }

    void testARowThatOnlySaysATestStartedIsNotAResult()
    {
        // The delegate wrote a result's word in the colour of its severity,
        // and a "test started" row in the disabled colour instead - it is not
        // a pass or a failure, it is the heading above them.
        TestResult start("someId", "someTest");
        start.setResult(ResultType::TestStart);
        TestResult failure("someId", "someTest");
        failure.setResult(ResultType::Fail);

        const QColor startColor
            = TestResultItem(start).data(0, TestResultItem::ResultColorRole).value<QColor>();
        const QColor failureColor
            = TestResultItem(failure).data(0, TestResultItem::ResultColorRole).value<QColor>();

        QVERIFY(startColor.isValid());
        QVERIFY(failureColor.isValid());
        QCOMPARE(failureColor, TestResult::colorForType(ResultType::Fail));

        // The disabled colour specifically, as the widget used the palette's
        // mid colour: colorForType() has an answer for TestStart too, and it
        // is the wrong one - it makes a heading look like an outcome.
        QCOMPARE(startColor, Utils::creatorColor(Utils::Theme::TextColorDisabled));
    }
};

QObject *createTestResultModelTest()
{
    return new TestResultModelTest;
}

#endif // WITH_TESTS

} // namespace Autotest::Internal

#ifdef WITH_TESTS
#include "testresultmodel.moc"
#endif

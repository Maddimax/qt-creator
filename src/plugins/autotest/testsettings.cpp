// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "testsettings.h"

#include "autotestconstants.h"
#include "autotestplugin.h"
#include "autotesttr.h"
#include "testframeworkmanager.h"
#include "testframeworkmanager.h"
#include "testsettings.h"
#include "testtreemodel.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspectwidgets.h>
#include <utils/algorithm.h>
#include <utils/guiutils.h>
#include <utils/id.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>
#include <utils/qtcsettings.h>

#include <QHeaderView>

using namespace Utils;

namespace Autotest::Internal  {

static const char groupSuffix[]                 = ".group";

constexpr int defaultTimeout = 60000;

TestSettings &testSettings()
{
    static TestSettings theSettings;
    return theSettings;
}

enum TestBaseInfo
{
    BaseId = Qt::UserRole,
    BaseType
};

// A row per registered test framework and test tool: a check box that turns it
// on, and for a framework a second one that groups its tests by folder. The
// rows come from the registry; the check states are the aspect's volatile
// value, which is why they are written straight into it.
class FrameworksModel : public QAbstractTableModel
{
public:
    enum Column { ColumnActive, ColumnGrouping, ColumnCount };

    struct Row
    {
        Id id;
        QString name;
        QString groupingToolTip;
        bool isFramework = true;
    };

    FrameworksModel(FrameworksAspect::Data *data, QObject *parent)
        : QAbstractTableModel(parent)
        , m_data(data)
    {}

    // The registry is filled while the plugin starts up, so the rows are taken
    // when the settings are read rather than in the constructor.
    void refresh()
    {
        beginResetModel();
        m_rows.clear();
        for (const ITestFramework *framework : TestFrameworkManager::registeredFrameworks()) {
            QString groupingToolTip = framework->groupingToolTip();
            if (groupingToolTip.isEmpty())
                groupingToolTip = Tr::tr("Enable or disable grouping of test cases by folder.");
            m_rows.append({framework->id(), framework->displayName(), groupingToolTip, true});
        }
        for (const ITestTool *testTool : TestFrameworkManager::registeredTestTools())
            m_rows.append({testTool->id(), testTool->displayName(), {}, false});
        endResetModel();
    }

    void reread()
    {
        if (m_rows.isEmpty())
            return;
        emit dataChanged(index(0, ColumnActive),
                         index(m_rows.size() - 1, ColumnGrouping),
                         {Qt::CheckStateRole});
    }

    // Whether anything is ticked, and whether both kinds are.
    bool anyActive() const
    {
        return Utils::anyOf(m_rows, [this](const Row &row) { return isActive(row); });
    }

    bool mixesFrameworksAndTools() const
    {
        const bool framework = Utils::anyOf(m_rows, [this](const Row &row) {
            return row.isFramework && isActive(row);
        });
        const bool tool = Utils::anyOf(m_rows, [this](const Row &row) {
            return !row.isFramework && isActive(row);
        });
        return framework && tool;
    }

    int rowCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    int columnCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        const Row &row = m_rows.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            return index.column() == ColumnActive ? row.name : QString();
        case Qt::CheckStateRole:
            if (!flags(index).testFlag(Qt::ItemIsUserCheckable))
                return {};
            return checkState(index) ? Qt::Checked : Qt::Unchecked;
        case Qt::ToolTipRole:
            if (index.column() == ColumnActive) {
                return Tr::tr("Enable or disable test frameworks to be handled by the "
                              "AutoTest plugin.");
            }
            return row.groupingToolTip;
        case AspectTable::EditableRole:
            return AspectTable::isWritable(flags(index));
        case AspectTable::CheckableRole:
            return flags(index).testFlag(Qt::ItemIsUserCheckable);
        default:
            return {};
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (role != Qt::CheckStateRole || !flags(index).testFlag(Qt::ItemIsUserCheckable))
            return false;
        const Row &row = m_rows.at(index.row());
        const bool on = value.toInt() == Qt::Checked;
        if (index.column() == ColumnGrouping)
            m_data->frameworksGrouping.insert(row.id, on);
        else if (row.isFramework)
            m_data->frameworks.insert(row.id, on);
        else
            m_data->tools.insert(row.id, on);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Vertical)
            return {};
        if (role == Qt::ToolTipRole) {
            return section == ColumnActive
                       ? Tr::tr("Selects the test frameworks to be handled by the AutoTest plugin.")
                       : Tr::tr("Enables grouping of test cases.");
        }
        if (role != Qt::DisplayRole)
            return {};
        return section == ColumnActive ? Tr::tr("Framework") : Tr::tr("Group");
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        const Qt::ItemFlags flags = QAbstractTableModel::flags(index);
        // A test tool has no tests to group, so its second cell is empty.
        if (index.column() == ColumnGrouping && !m_rows.at(index.row()).isFramework)
            return flags;
        return flags | Qt::ItemIsUserCheckable;
    }

private:
    bool isActive(const Row &row) const
    {
        return row.isFramework ? m_data->frameworks.value(row.id, false)
                               : m_data->tools.value(row.id, false);
    }

    bool checkState(const QModelIndex &index) const
    {
        const Row &row = m_rows.at(index.row());
        if (index.column() == ColumnGrouping)
            return m_data->frameworksGrouping.value(row.id, false);
        return isActive(row);
    }

    FrameworksAspect::Data *m_data = nullptr;
    QList<Row> m_rows;
};

static bool operator==(const FrameworksAspect::Data &first, const FrameworksAspect::Data &second)
{
    return first.frameworks == second.frameworks
           && first.frameworksGrouping == second.frameworksGrouping
           && first.tools == second.tools;
}

class FrameworksAspectPrivate
{
public:
    explicit FrameworksAspectPrivate(FrameworksAspect *aspect)
        : m_model(&m_volatileData, aspect)
    {}

    // What was applied, and what the check boxes hold now.
    FrameworksAspect::Data m_data;
    FrameworksAspect::Data m_volatileData;
    FrameworksModel m_model;
};

FrameworksAspect::FrameworksAspect(AspectContainer *container)
    : BaseAspect(container)
    , d(new FrameworksAspectPrivate(this))
{
    setQmlName("Frameworks");
    setToolTip(Tr::tr("Selects the test frameworks to be handled by the AutoTest plugin."));

    // The warning depends on what is ticked, and a check box is ticked through
    // the model, so the model says when to look again.
    connect(&d->m_model, &QAbstractItemModel::dataChanged, this, [this] {
        Utils::checkSettingsDirty();
        emit volatileValueChanged();
    });
}

FrameworksAspect::~FrameworksAspect()
{
    delete d;
}

bool FrameworksAspect::framework(Id id) const
{
    return d->m_data.frameworks.value(id, false);
}

bool FrameworksAspect::frameworkGrouping(Id id) const
{
    return d->m_data.frameworksGrouping.value(id, false);
}

bool FrameworksAspect::tool(Id id) const
{
    return d->m_data.tools.value(id, false);
}

AspectPresentation FrameworksAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::Table;
    // The rows are whatever registered itself.
    p.allowAdding = false;
    p.allowRemoving = false;
    return p;
}

QAbstractItemModel *FrameworksAspect::tableModel()
{
    return &d->m_model;
}

QString FrameworksAspect::warning() const
{
    if (!d->m_model.anyActive())
        return Tr::tr("No active test frameworks or tools.");
    if (d->m_model.mixesFrameworksAndTools())
        return Tr::tr("Mixing test frameworks and test tools.");
    return {};
}

QString FrameworksAspect::warningToolTip() const
{
    if (!d->m_model.anyActive()) {
        return Tr::tr("You will not be able to use the AutoTest plugin "
                      "without having at least one active test framework.");
    }
    if (d->m_model.mixesFrameworksAndTools()) {
        return Tr::tr("Mixing test frameworks and test tools can lead "
                      "to duplicating run information when using "
                      "\"Run All Tests\", for example.");
    }
    return {};
}

void FrameworksAspect::apply()
{
    const Data tmp = d->m_volatileData;

    const QList<Utils::Id> changedIds = Utils::filtered(tmp.frameworksGrouping.keys(),
       [this, &tmp](Id id) {
        return tmp.frameworksGrouping[id] != d->m_data.frameworksGrouping[id];
    });

    d->m_data = tmp;

    writeSettings();

    for (ITestFramework *framework : TestFrameworkManager::registeredFrameworks()) {
        framework->setActive(tmp.frameworks.value(framework->id(), false));
        framework->setGrouping(tmp.frameworksGrouping.value(framework->id(), false));
    }

    for (ITestTool *testTool : TestFrameworkManager::registeredTestTools())
        testTool->setActive(tmp.tools.value(testTool->id(), false));

    TestTreeModel::instance()->synchronizeTestFrameworks();
    TestTreeModel::instance()->synchronizeTestTools();
    if (!changedIds.isEmpty())
        TestTreeModel::instance()->rebuild(changedIds);
}

void FrameworksAspect::cancel()
{
    d->m_volatileData = d->m_data;
    d->m_model.reread();
}

bool FrameworksAspect::isDirty() const
{
    return !(d->m_volatileData == d->m_data);
}

void FrameworksAspect::writeSettings() const
{
    QtcSettings &s = Utils::userSettings();
    s.beginGroup(Constants::SETTINGSGROUP);

    // store frameworks and their current active and grouping state
    for (auto it = d->m_data.frameworks.cbegin(); it != d->m_data.frameworks.cend(); ++it) {
        const Utils::Id &id = it.key();
        s.setValue(id.toKey(), it.value());
        s.setValue(id.toKey() + groupSuffix, d->m_data.frameworksGrouping.value(id));
    }
    // ..and the testtools as well
    for (auto it = d->m_data.tools.cbegin(); it != d->m_data.tools.cend(); ++it)
        s.setValue(it.key().toKey(), it.value());
    s.endGroup();
}

void FrameworksAspect::readSettings()
{
    QtcSettings &s = Utils::userSettings();
    s.beginGroup(Constants::SETTINGSGROUP);

    // try to get settings for registered frameworks
    const TestFrameworks &registered = TestFrameworkManager::registeredFrameworks();
    d->m_data.frameworks.clear();
    d->m_data.frameworksGrouping.clear();
    for (const ITestFramework *framework : registered) {
        // get their active state
        const Id id = framework->id();
        const Key key = id.toKey();
        d->m_data.frameworks.insert(id, s.value(key, framework->active()).toBool());
        // and whether grouping is enabled
        d->m_data.frameworksGrouping.insert(id,
                                            s.value(key + groupSuffix, framework->grouping())
                                                .toBool());
    }
    // ..and for test tools as well
    const TestTools &registeredTools = TestFrameworkManager::registeredTestTools();
    d->m_data.tools.clear();
    for (const ITestTool *testTool : registeredTools) {
        const Id id = testTool->id();
        d->m_data.tools.insert(id, s.value(id.toKey(), testTool->active()).toBool());
    }
    s.endGroup();

    d->m_volatileData = d->m_data;
    d->m_model.refresh();
}

TestSettings::TestSettings()
{
    setAutoApply(false);

    setSettingsGroup(Constants::SETTINGSGROUP);

    scanThreadLimit.setSettingsKey("ScanThreadLimit");
    scanThreadLimit.setDefaultValue(0);
    scanThreadLimit.setRange(0, QThread::idealThreadCount());
    scanThreadLimit.setSpecialValueText("Automatic");
    scanThreadLimit.setLabelText(Tr::tr("Scan threads:"));
    scanThreadLimit.setToolTip(Tr::tr("Number of worker threads used when scanning for tests."));

    useTimeout.setSettingsKey("UseTimeout");
    useTimeout.setDefaultValue(false);
    useTimeout.setLabelText(Tr::tr("Timeout:"));
    useTimeout.setToolTip(Tr::tr("Use a timeout while executing test cases."));

    timeout.setSettingsKey("Timeout");
    timeout.setDefaultValue(defaultTimeout);
    timeout.setRange(5000, 36'000'000); // 36 Mio ms = 36'000 s = 10 h
    timeout.setSuffix(Tr::tr(" s")); // we show seconds, but store milliseconds
    timeout.setDisplayScaleFactor(1000);
    timeout.setToolTip(Tr::tr("Timeout used when executing test cases. This will apply "
                              "for each test case on its own, not the whole project. "
                              "Overrides test framework or build system defaults."));

    omitInternalMsg.setSettingsKey("OmitInternal");
    omitInternalMsg.setDefaultValue(true);
    omitInternalMsg.setLabelText(Tr::tr("Omit internal messages"));
    omitInternalMsg.setToolTip(Tr::tr("Hides internal messages by default. "
        "You can still enable them by using the test results filter."));

    omitRunConfigWarn.setSettingsKey("OmitRCWarnings");
    omitRunConfigWarn.setLabelText(Tr::tr("Omit run configuration warnings"));
    omitRunConfigWarn.setToolTip(Tr::tr("Hides warnings related to a deduced run configuration."));

    limitResultOutput.setSettingsKey("LimitResultOutput");
    limitResultOutput.setDefaultValue(true);
    limitResultOutput.setLabelText(Tr::tr("Limit result output"));
    limitResultOutput.setToolTip(Tr::tr("Limits result output to 100000 characters."));

    limitResultDescription.setSettingsKey("LimitResultDescription");
    limitResultDescription.setLabelText(Tr::tr("Limit result description:"));
    limitResultDescription.setToolTip(
        Tr::tr("Limit number of lines shown in test result tooltip and description."));

    resultDescriptionMaxSize.setSettingsKey("ResultDescriptionMaxSize");
    resultDescriptionMaxSize.setDefaultValue(10);
    resultDescriptionMaxSize.setRange(1, 100000);

    autoScroll.setSettingsKey("AutoScrollResults");
    autoScroll.setDefaultValue(true);
    autoScroll.setLabelText(Tr::tr("Automatically scroll results"));
    autoScroll.setToolTip(Tr::tr("Automatically scrolls down when new items are added "
                                 "and scrollbar is at bottom."));

    processArgs.setSettingsKey("ProcessArgs");
    processArgs.setLabelText(Tr::tr("Process arguments"));
    processArgs.setToolTip(
        Tr::tr("Allow passing arguments specified on the respective run configuration.\n"
               "Warning: this is an experimental feature and might lead to failing to "
               "execute the test executable."));

    displayApplication.setSettingsKey("DisplayApp");
    displayApplication.setLabelText(Tr::tr("Group results by application"));

    popupOnStart.setSettingsKey("PopupOnStart");
    popupOnStart.setLabelText(Tr::tr("Open results when tests start"));
    popupOnStart.setToolTip(
        Tr::tr("Displays test results automatically when tests are started."));

    popupOnFinish.setSettingsKey("PopupOnFinish");
    popupOnFinish.setDefaultValue(true);
    popupOnFinish.setLabelText(Tr::tr("Open results when tests finish"));
    popupOnFinish.setToolTip(
        Tr::tr("Displays test results automatically when tests are finished."));

    popupOnFail.setSettingsKey("PopupOnFail");
    popupOnFail.setLabelText(Tr::tr("Only for unsuccessful test runs"));
    popupOnFail.setToolTip(Tr::tr("Displays test results only if the test run contains "
                                  "failed, fatal or unexpectedly passed tests."));

    // UI not in settings page, but inside the filter menu of the navigation widget
    showTreeFilterTextInput.setSettingsKey("ShowTreeFilter");
    showTreeFilterTextInput.setDefaultValue(true);

    runAfterBuild.setSettingsKey("RunAfterBuild");
    runAfterBuild.setDisplayStyle(Utils::SelectionAspect::DisplayStyle::ComboBox);
    runAfterBuild.setLabelText(Tr::tr("Automatically run:"));
    runAfterBuild.setToolTip(Tr::tr("Runs chosen tests automatically if a build succeeded."));
    runAfterBuild.addOption(Tr::tr("No Tests"));
    runAfterBuild.addOption(Tr::tr("All", "Run tests after build"));
    runAfterBuild.addOption(Tr::tr("Selected"));

    frameworksWarning.setQmlName("FrameworksWarning");
    frameworksWarning.setIconType(InfoType::Warning);
    frameworksWarning.setWordWrap(true);

    // The warning is about what is ticked, so it follows the aspect rather than
    // being recomputed by whoever draws the page.
    const auto updateFrameworksWarning = [this] {
        frameworksWarning.setText(frameworks.warning());
        frameworksWarning.setToolTip(frameworks.warningToolTip());
        frameworksWarning.setVisible(!frameworks.warning().isEmpty());
    };
    connect(&frameworks, &BaseAspect::volatileValueChanged, this, updateFrameworksWarning);

    resetChoiceCache.setQmlName("ResetChoiceCache");
    resetChoiceCache.setActionText(Tr::tr("Reset Cached Choices"));
    resetChoiceCache.setToolTip(Tr::tr("Clear all cached choices of run configurations for "
                                       "tests where the executable could not be deduced."));
    resetChoiceCache.setAction([] { clearChoiceCache(); });

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/AutoTest/TestSettingsPage.qml"));


    readSettings();

    timeout.setEnabler(&useTimeout);
    resultDescriptionMaxSize.setEnabler(&limitResultDescription);
    popupOnFail.setEnabler(&popupOnFinish);

    // readSettings() filled the aspect, so the warning has something to say.
    updateFrameworksWarning();
}

RunAfterBuildMode TestSettings::runAfterBuildMode() const
{
    return static_cast<RunAfterBuildMode>(runAfterBuild());
}

// TestSettingsPage

class TestSettingsPage final : public Core::IOptionsPage
{
public:
    TestSettingsPage()
    {
        setId(Constants::AUTOTEST_SETTINGS_ID);
        setDisplayName(Tr::tr("General"));
        setCategory(Constants::AUTOTEST_SETTINGS_CATEGORY);
        setSettingsProvider([] { return &testSettings(); });
    }
};

void setupTestSettings()
{
    static TestSettingsPage theTestSettingsPage;
}

} // Autotest::Internal

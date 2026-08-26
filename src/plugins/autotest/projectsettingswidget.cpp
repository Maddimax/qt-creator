// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectsettingswidget.h"

#include "autotestplugin.h"
#include "autotesttr.h"
#include "testcodeparser.h"
#include "testprojectsettings.h"
#include "testtreemodel.h"

#include <projectexplorer/project.h>
#include <projectexplorer/projectpanelfactory.h>

#include <utils/aspectwidgets.h>
#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>

#include <QAbstractTableModel>
#include <QTimer>

using namespace ProjectExplorer;

namespace Autotest::Internal {

// One row per test framework and test tool, ticked when this project runs it.
// A table aspect rather than a tree widget: which are active is the project
// settings' answer, and both backends read it from the same model.
class ActiveTestBasesModel final : public QAbstractTableModel
{
public:
    ActiveTestBasesModel(TestProjectSettings *settings, QObject *parent)
        : QAbstractTableModel(parent)
        , m_settings(settings)
    {
        reload();
    }

    // What was ticked, and what kind of thing it was, so that only what
    // changed is synchronized.
    std::function<void(int type)> onActivated;

    void reload()
    {
        beginResetModel();
        m_rows.clear();
        const ActiveTestFrameworks frameworks = m_settings->activeTestFrameworks();
        const TestFrameworks sorted = Utils::sorted(frameworks.keys(), &ITestFramework::priority);
        for (ITestFramework * const framework : sorted) {
            m_rows.append({framework->displayName(), framework->id(),
                           int(ITestBase::Framework), frameworks.value(framework)});
        }
        // Test tools have no priority to sort by, so they are listed the way
        // the tree listed them.
        const ActiveTestTools tools = m_settings->activeTestTools();
        for (auto it = tools.cbegin(), end = tools.cend(); it != end; ++it)
            m_rows.append({it.key()->displayName(), it.key()->id(), int(ITestBase::Tool), it.value()});
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }

    int columnCount(const QModelIndex & = {}) const override { return 1; }

    // Unnamed on purpose: one column that is the thing itself, which the tree
    // showed with its header hidden.
    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return {};
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() >= m_rows.size())
            return {};
        const Row &row = m_rows.at(index.row());
        if (role == Qt::DisplayRole)
            return row.name;
        if (role == Qt::CheckStateRole)
            return row.active ? Qt::Checked : Qt::Unchecked;
        if (role == Utils::AspectTable::CheckableRole)
            return true;
        if (role == Utils::AspectTable::EditableRole)
            return Utils::AspectTable::isWritable(flags(index));
        return {};
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (role != Qt::CheckStateRole || !index.isValid() || index.row() >= m_rows.size())
            return false;
        Row &row = m_rows[index.row()];
        const bool active = value.toInt() == Qt::Checked;
        if (row.active == active)
            return false;
        row.active = active;
        if (row.type == ITestBase::Framework)
            m_settings->activateFramework(row.id, active);
        else
            m_settings->activateTestTool(row.id, active);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        if (onActivated)
            onActivated(row.type);
        return true;
    }

    // A Qt Quick cell reads its roles by name and cannot see flags().
    QHash<int, QByteArray> roleNames() const override
    {
        return Utils::AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

private:
    struct Row
    {
        QString name;
        Utils::Id id;
        int type = 0;
        bool active = false;
    };

    TestProjectSettings * const m_settings;
    QList<Row> m_rows;
};

class ActiveTestBasesAspect final : public Utils::BaseAspect
{
public:
    explicit ActiveTestBasesAspect(TestProjectSettings *settings)
        : m_model(settings, this)
    {}

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = BaseAspect::presentation();
        p.control = Utils::AspectControls::Table;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

    ActiveTestBasesModel &model() { return m_model; }

private:
    // Parented: a model handed to QML with no parent belongs to the engine.
    ActiveTestBasesModel m_model;
};

// What the panel shows. The flag is kept out of the settings container because
// only part of what it holds is turned off by it - the path patterns are the
// project's whatever the global settings say. See ProjectCommentsPanel.
class TestProjectPanel final : public Utils::AspectContainer
{
public:
    explicit TestProjectPanel(Project *project)
        : m_settings(testProjectSettings(project))
        , m_activeTestBases(m_settings)
    {
        // Before registering: insertAspect() forces the container's own
        // auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/AutoTest/TestProjectPanel.qml"));

        m_settings->useGlobalSettings.setQmlName("UseGlobalSettings");
        registerAspect(&m_settings->useGlobalSettings);

        m_activeTestBases.setQmlName("ActiveTestBases");
        m_activeTestBases.setLabelText(Tr::tr("Active Test Frameworks"));
        registerAspect(&m_activeTestBases);

        m_settings->setQmlName("Settings");
        registerAspect(m_settings);

        // Behaviour, not layout. Ticking something takes effect once the user
        // has stopped, because rebuilding the test tree is not cheap.
        m_syncTimer.setSingleShot(true);
        connect(&m_syncTimer, &QTimer::timeout, this, [this] {
            TestTreeModel * const model = TestTreeModel::instance();
            if (m_syncType & ITestBase::Framework)
                model->synchronizeTestFrameworks();
            if (m_syncType & ITestBase::Tool)
                model->synchronizeTestTools();
            m_syncType = ITestBase::None;
        });
        m_activeTestBases.model().onActivated = [this](int type) { scheduleSync(type); };

        updateForUseGlobal();
        m_settings->useGlobalSettings.addOnChanged(this, [this] {
            updateForUseGlobal();
            scheduleSync(ITestBase::Framework | ITestBase::Tool);
        });

        // The patterns are the project's own, so a change means a rescan
        // whatever the global settings say.
        m_settings->limitToFilter.addOnChanged(this, [] { rescan(); });
        m_settings->pathFilters.addOnChanged(this, [] { rescan(); });
    }

    static Utils::Key extraDataKey() { return "TestProjectPanel"; }

private:
    static void rescan() { TestTreeModel::instance()->parser()->emitUpdateTestTree(); }

    void scheduleSync(int type)
    {
        m_syncType |= type;
        m_syncTimer.start(3000);
    }

    // What the global settings decide, and what stays the project's: the path
    // patterns were explicitly outside them in the widget form too.
    void updateForUseGlobal()
    {
        const bool useGlobal = m_settings->useGlobalSettings();
        m_activeTestBases.setEnabled(!useGlobal);
        m_settings->runAfterBuild.setEnabled(!useGlobal);
    }

    TestProjectSettings * const m_settings;
    ActiveTestBasesAspect m_activeTestBases;
    QTimer m_syncTimer;
    int m_syncType = ITestBase::None;
};

static TestProjectPanel *testProjectPanel(Project *project)
{
    const Utils::Key key = TestProjectPanel::extraDataKey();
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new TestProjectPanel(project));
        project->setExtraData(key, v);
    }
    return v.value<TestProjectPanel *>();
}

class AutotestProjectPanelFactory final : public ProjectPanelFactory
{
public:
    AutotestProjectPanelFactory()
    {
        setPriority(666);
        setDisplayName(Tr::tr("Testing"));
        setSettingsProvider([](Project *project) {
            return testProjectPanel(project);
        });
    }
};

void setupAutotestProjectPanel()
{
    static AutotestProjectPanelFactory theAutotestProjectPanelFactory;
}

} // Autotest::Internal

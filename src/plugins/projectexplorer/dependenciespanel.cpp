// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dependenciespanel.h"

#include "project.h"
#include "projectexplorertr.h"
#include "projectmanager.h"
#include "projectpanelfactory.h"

#include <coreplugin/icore.h>
#include <coreplugin/session.h>

#include <utils/algorithm.h>
#include <utils/detailswidget.h>
#include <utils/fsengine/fileiconprovider.h>

#include <QAbstractListModel>
#include <QCheckBox>
#include <QGridLayout>
#include <QMessageBox>
#include <QSize>
#include <QSpacerItem>
#include <QTreeView>

namespace ProjectExplorer::Internal {

class DependenciesModel : public QAbstractListModel
{
public:
    explicit DependenciesModel(Project *project, QObject *parent = nullptr)
        : QAbstractListModel(parent)
        , m_project(project)
    {
        resetModel();

        connect(ProjectManager::instance(), &ProjectManager::projectRemoved,
                this, &DependenciesModel::resetModel);
        connect(ProjectManager::instance(), &ProjectManager::projectAdded,
                this, &DependenciesModel::resetModel);
        connect(Core::SessionManager::instance(), &Core::SessionManager::sessionLoaded,
                this, &DependenciesModel::resetModel);
    }

    int rowCount(const QModelIndex &index) const override;
    int columnCount(const QModelIndex &index) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // A Qt Quick cell reads its roles by name and cannot see flags().
    QHash<int, QByteArray> roleNames() const override
    {
        return Utils::AspectTable::withRoleNames(QAbstractListModel::roleNames());
    }

private:
    void resetModel();

    Project *m_project;
    QList<Project *> m_projects;
};

void DependenciesModel::resetModel()
{
    beginResetModel();
    m_projects = ProjectManager::projects();
    m_projects.removeAll(m_project);
    Utils::sort(m_projects, [](Project *a, Project *b) {
        return a->displayName() < b->displayName();
    });
    endResetModel();
}

int DependenciesModel::rowCount(const QModelIndex &index) const
{
    return index.isValid() ? 0 : m_projects.isEmpty() ? 1 : m_projects.size();
}

int DependenciesModel::columnCount(const QModelIndex &index) const
{
    return index.isValid() ? 0 : 1;
}

QVariant DependenciesModel::data(const QModelIndex &index, int role) const
{
    if (m_projects.isEmpty())
        return role == Qt::DisplayRole
            ? Tr::tr("<No other projects in this session>")
            : QVariant();

    const Project *p = m_projects.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        return p->displayName();
    case Qt::ToolTipRole:
        return p->projectFilePath().toUserOutput();
    case Qt::CheckStateRole:
        return ProjectManager::hasDependency(m_project, p) ? Qt::Checked : Qt::Unchecked;
    case Qt::DecorationRole:
        return Utils::FileIconProvider::icon(p->projectFilePath());
    case Utils::AspectTable::CheckableRole:
        return flags(index).testFlag(Qt::ItemIsUserCheckable);
    case Utils::AspectTable::EditableRole:
        return Utils::AspectTable::isWritable(flags(index));
    default:
        return {};
    }
}

bool DependenciesModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role == Qt::CheckStateRole) {
        Project *p = m_projects.at(index.row());
        const auto c = static_cast<Qt::CheckState>(value.toInt());

        if (c == Qt::Checked) {
            if (ProjectManager::addDependency(m_project, p)) {
                emit dataChanged(index, index);
                return true;
            } else {
                QMessageBox::warning(Core::ICore::dialogParent(), Tr::tr("Unable to Add Dependency"),
                                     Tr::tr("This would create a circular dependency."));
            }
        } else if (c == Qt::Unchecked) {
            if (ProjectManager::hasDependency(m_project, p)) {
                ProjectManager::removeDependency(m_project, p);
                emit dataChanged(index, index);
                return true;
            }
        }
    }
    return false;
}

Qt::ItemFlags DependenciesModel::flags(const QModelIndex &index) const
{
    if (m_projects.isEmpty())
        return Qt::NoItemFlags;

    Qt::ItemFlags rc = QAbstractListModel::flags(index);
    if (index.column() == 0)
        rc |= Qt::ItemIsUserCheckable | Qt::ItemIsEditable;
    return rc;
}

//
// DependenciesView
//

// The other projects in the session, ticked where this one depends on them.
class DependenciesAspect final : public Utils::BaseAspect
{
public:
    explicit DependenciesAspect(Project *project)
        : m_model(project, this)
    {}

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = BaseAspect::presentation();
        p.control = Utils::AspectControls::Table;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

private:
    // Parented: a model handed to QML with no parent belongs to the engine.
    DependenciesModel m_model;
};

// What the panel shows. The two check boxes are the session's rather than the
// project's - ProjectManager keeps them - so they are read once and written
// through, which is what the check boxes did.
class DependenciesPanel final : public Utils::AspectContainer
{
public:
    explicit DependenciesPanel(Project *project)
        : m_dependencies(project)
    {
        // Before registering: insertAspect() forces the container's own
        // auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/DependenciesPanel.qml"));

        m_dependencies.setQmlName("Dependencies");
        registerAspect(&m_dependencies);

        m_cascadeSetActive.setQmlName("CascadeSetActive");
        m_cascadeSetActive.setLabel(Tr::tr("Synchronize configuration"),
                                    Utils::BoolAspect::LabelPlacement::AtCheckBox);
        m_cascadeSetActive.setToolTip(
            Tr::tr("Synchronize active kit, build, and deploy configuration between projects."));
        m_cascadeSetActive.setValue(ProjectManager::isProjectConfigurationCascading());
        registerAspect(&m_cascadeSetActive);

        m_deployDependencies.setQmlName("DeployDependencies");
        m_deployDependencies.setLabel(Tr::tr("Deploy dependencies"),
                                      Utils::BoolAspect::LabelPlacement::AtCheckBox);
        m_deployDependencies.setToolTip(
            Tr::tr("Do not just build dependencies, but deploy them as well."));
        m_deployDependencies.setValue(ProjectManager::deployProjectDependencies());
        registerAspect(&m_deployDependencies);

        // Behaviour, not layout.
        m_cascadeSetActive.addOnChanged(this, [this] {
            ProjectManager::setProjectConfigurationCascading(m_cascadeSetActive());
        });
        m_deployDependencies.addOnChanged(this, [this] {
            ProjectManager::setDeployProjectDependencies(m_deployDependencies());
        });
    }

    static Utils::Key extraDataKey() { return "DependenciesPanel"; }

private:
    DependenciesAspect m_dependencies;
    Utils::BoolAspect m_cascadeSetActive;
    Utils::BoolAspect m_deployDependencies;
};

static DependenciesPanel *dependenciesPanel(Project *project)
{
    const Utils::Key key = DependenciesPanel::extraDataKey();
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new DependenciesPanel(project));
        project->setExtraData(key, v);
    }
    return v.value<DependenciesPanel *>();
}

class DependenciesProjectPanelFactory final : public ProjectPanelFactory
{
public:
    DependenciesProjectPanelFactory()
    {
        setPriority(50);
        setDisplayName(Tr::tr("Dependencies"));
        setSettingsProvider([](Project *project) {
            return dependenciesPanel(project);
        });
    }
};

void setupDependenciesProjectPanel()
{
    static DependenciesProjectPanelFactory theDependenciesProjectPanelFactory;
}

} // ProjectExplorer::Internal

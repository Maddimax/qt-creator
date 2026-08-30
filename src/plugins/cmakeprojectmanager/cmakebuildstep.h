// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cmakeabstractprocessstep.h"
#include <projectexplorer/environmentaspect.h>

#include <utils/treemodel.h>

namespace Utils {
class CommandLine;
class StringAspect;
} // Utils

namespace CMakeProjectManager::Internal {

class CMakeBuildStep;

class CMakeTargetItem : public Utils::TreeItem
{
public:
    CMakeTargetItem() = default;
    CMakeTargetItem(const QString &target, CMakeBuildStep *step, bool special);

private:
    QVariant data(int column, int role) const final;
    bool setData(int column, const QVariant &data, int role) final;
    Qt::ItemFlags flags(int column) const final;

    QString m_target;
    CMakeBuildStep *m_step = nullptr;
    bool m_special = false;
};

class CMakeBuildStep final : public CMakeAbstractProcessStep
{
    Q_OBJECT

public:
    // The role names a Qt Quick cell reads: BaseTreeModel leaves out the ones
    // that say a cell is checkable and what it is checked to, and the targets
    // are exactly a list of check boxes.
    class TargetsModel final : public Utils::TreeModel<Utils::TreeItem, CMakeTargetItem>
    {
    public:
        QHash<int, QByteArray> roleNames() const override;
    };

    // Which targets to build, from the model the items already read and write
    // the step through.
    class TargetsAspect final : public Utils::BaseAspect
    {
    public:
        TargetsAspect(Utils::AspectContainer *container, QAbstractItemModel *model);

        Utils::AspectPresentation presentation() const override;
        QAbstractItemModel *tableModel() override { return m_model; }

    private:
        QAbstractItemModel * const m_model;
    };


    CMakeBuildStep(ProjectExplorer::BuildStepList *bsl, Utils::Id id);

    QStringList buildTargets() const;
    void setBuildTargets(const QStringList &target) override;

    bool buildsBuildTarget(const QString &target) const;
    void setBuildsBuildTarget(const QString &target, bool on);

    void toMap(Utils::Store &map) const override;

    QString cleanTarget() const;
    QString allTarget() const ;
    QString installTarget() const;
    static QStringList specialTargets(bool allCapsTargets);

    QString activeRunConfigTarget() const;

    void setBuildPreset(const QString &preset);

    Utils::Environment environment() const;
    void setUserEnvironmentChanges(const Utils::EnvironmentChanges &diff);
    Utils::EnvironmentChanges userEnvironmentChanges() const;
    bool useClearEnvironment() const;
    void setUseClearEnvironment(bool b);
    void updateAndEmitEnvironmentChanged();

    Utils::Environment baseEnvironment() const;
    QString baseEnvironmentText() const;

    void setCMakeArguments(const QStringList &cmakeArguments);
    void setToolArguments(const QStringList &nativeToolArguments);

    void setConfiguration(const QString &configuration);

    Utils::StringAspect cmakeArguments{this};
    Utils::StringAspect toolArguments{this};
    Utils::BoolAspect useiOSAutomaticProvisioningUpdates{this};
    Utils::BoolAspect useStaging{this};
    Utils::FilePathAspect stagingDir{this};

signals:
    void buildTargetsChanged();
    void environmentChanged();

private:
    Utils::CommandLine cmakeCommand() const;

    void fromMap(const Utils::Store &map) override;

    bool init() override;
    void setupOutputFormatter(Utils::OutputFormatter *formatter) override;
    QtTaskTree::GroupItem runRecipe() final;

    Utils::FilePath cmakeExecutable() const;
    QString currentInstallPrefix() const;

    QString defaultBuildTarget() const;
    bool isCleanStep() const;

    void handleBuildTargetsChanges(bool success);
    // What the step works out about itself: which of the staging settings can
    // be used, and the summary line the collapsed step shows. It used to live
    // in createConfigWidget(), so the summary was whatever it had been until
    // somebody opened the step.
    void updateDetails();
    void updateEnvironmentVisibility();
    void refreshEnvironment();

    void recreateBuildTargetsModel();
    void updateBuildTargetsModel();
    void updateDeploymentData();

    QStringList processSubDirStagingSingleTarget(const QStringList &targets);

    friend class CMakeBuildStepConfigWidget;
    QStringList m_buildTargets; // Convention: Empty string member signifies "Current executable"

    QString m_allTarget = "all";
    QString m_installTarget = "install";

    TargetsModel m_buildTargetModel;
    TargetsAspect m_targets{this, &m_buildTargetModel};

    // The environment a preset build runs in: the same editor every page that
    // edits an environment draws, plus what it starts from.
    Utils::BoolAspect m_clearEnvironment{this};
    ProjectExplorer::EnvironmentEditorAspect m_environmentEditor{this};

    Utils::Environment m_environment;
    Utils::EnvironmentChanges m_userEnvironmentChanges;
    bool m_clearSystemEnvironment = false;
    QString m_buildPreset;
    std::optional<QString> m_configuration;
};

void setupCMakeBuildStep();

#ifdef WITH_TESTS
QObject *createCMakeBuildStepPageTest();
#endif

} // CMakeProjectManager::Internal

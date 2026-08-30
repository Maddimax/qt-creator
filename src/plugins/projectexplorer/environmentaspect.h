// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"

#include "devicesupport/idevicefwd.h"
#include "runconfiguration.h"

#include <utils/aspects.h>
#include <utils/environmentmodel.h>
#include <utils/guard.h>
#include <utils/environment.h>
#include <utils/store.h>

namespace ProjectExplorer {

// The variables an environment ends up with, as a table a Qt Quick page can
// draw. The rows are the model's; which one is current is the view's, and the
// buttons beside it need to know.
class PROJECTEXPLORER_EXPORT EnvironmentItemsAspect final : public Utils::BaseAspect
{
    Q_OBJECT

public:
    explicit EnvironmentItemsAspect(Utils::AspectContainer *container = nullptr);

    Utils::AspectPresentation presentation() const override;
    QAbstractItemModel *tableModel() override;

    Q_INVOKABLE void setCurrentRow(int row);
    QModelIndex currentIndex() const;

    Utils::EnvironmentModel &model() { return m_model; }

signals:
    void currentRowChanged();

private:
    // Parented: a model handed to QML with no parent belongs to the engine.
    Utils::EnvironmentModel m_model{this};
    int m_currentRow = -1;
};

// Editing a set of environment changes: the resulting variables as a table,
// the operations on whichever of them is current, and the same changes as
// text. What the changes belong to is the caller's business - a project's
// additional environment, a run configuration's - so it hands over what to
// read them from and where to put them back.
//
// Drawn by EnvironmentEditor.qml, which any page can instantiate over the
// names below.
class PROJECTEXPLORER_EXPORT EnvironmentEditorAspect final : public Utils::AspectContainer
{
    Q_OBJECT

public:
    explicit EnvironmentEditorAspect(Utils::AspectContainer *container = nullptr);

    void setBaseEnvironment(const Utils::Environment &env);

    Utils::EnvironmentChanges changes() const;
    // Sets what is being edited without reporting it back as an edit.
    void setChanges(const Utils::EnvironmentChanges &changes);

signals:
    // The user changed something. Not BaseAspect::changed(), which says the
    // aspect's own value changed and this one has none.
    void changesEdited(const Utils::EnvironmentChanges &changes);

private:
    QString currentName() const;
    void showChangesAsText();
    void editCurrent();
    void amendPathList(Utils::EnvironmentItem::Operation op);
    void updateActions();

    EnvironmentItemsAspect m_variables;
    Utils::StringAspect m_changes;
    Utils::ActionAspect m_edit;
    Utils::ActionAspect m_add;
    Utils::ActionAspect m_reset;
    Utils::ActionAspect m_unset;
    Utils::ActionAspect m_toggle;
    Utils::ActionAspect m_appendPath;
    Utils::ActionAspect m_prependPath;
    // The table and the text are two views of one thing; writing either must
    // not come back as a change to the other.
    Utils::Guard m_updating;
};

class PROJECTEXPLORER_EXPORT EnvironmentAspect : public Utils::BaseAspect
{
    Q_OBJECT

public:
    EnvironmentAspect(Utils::AspectContainer *container = nullptr);

    enum DeviceSelector { HostDevice, BuildDevice, RunDevice };
    void setDeviceSelector(Kit *kit, DeviceSelector selector);

    // The environment including the user's modifications.
    Utils::Environment environment() const;
    Utils::Environment expandedEnvironment(const Utils::MacroExpander &expander) const;

    // Environment including modifiers, but without explicit user changes.
    Utils::Environment modifiedBaseEnvironment() const;

    int baseEnvironmentBase() const;
    void setBaseEnvironmentBase(int base);

    Utils::EnvironmentChanges userEnvironmentChanges() const;
    void setUserEnvironmentChanges(const Utils::EnvironmentChanges &diff);

    int addSupportedBaseEnvironment(const QString &displayName,
                                    const std::function<Utils::Environment()> &getter);
    int addPreferredBaseEnvironment(const QString &displayName,
                                    const std::function<Utils::Environment()> &getter);

    void setSupportForBuildEnvironment(BuildConfiguration *bc);

    QString currentDisplayName() const;

    const QStringList displayNames() const;

    using EnvironmentModifier = std::function<void(Utils::Environment &)>;
    void addModifier(const EnvironmentModifier &);

    bool isLocal() const { return m_isLocal; }

    IDeviceConstPtr device() const;

    bool isPrintOnRunAllowed() const { return m_allowPrintOnRun; }
    bool isPrintOnRunEnabled() const { return m_printOnRun; }
    void setPrintOnRun(bool enabled) { m_printOnRun = enabled; }

    struct Data : BaseAspect::Data
    {
        Utils::Environment environment;
    };

signals:
    void baseEnvironmentChanged();
    void userEnvironmentChangesChanged(const Utils::EnvironmentChanges &diff);
    void environmentChanged();
    void userChangesUpdateRequested() const;
    void devicePotentiallyChanged();

protected:
    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;

    void setIsLocal(bool local) { m_isLocal = local; }
    void setAllowPrintOnRun(bool allow) { m_allowPrintOnRun = allow; }

    static constexpr char BASE_KEY[] = "PE.EnvironmentAspect.Base";
    static constexpr char CHANGES_KEY[] = "PE.EnvironmentAspect.Changes";

private:
    virtual void handleKitUpdate() {}

    // One possible choice in the Environment aspect.
    struct BaseEnvironment {
        Utils::Environment unmodifiedBaseEnvironment() const;

        std::function<Utils::Environment()> getter;
        QString displayName;
    };

    Utils::EnvironmentChanges m_userChanges;
    QList<EnvironmentModifier> m_modifiers;
    QList<BaseEnvironment> m_baseEnvironments;
    int m_base = -1;
    bool m_isLocal = false;
    bool m_allowPrintOnRun = true;
    bool m_printOnRun = false;
    Kit *m_kit = nullptr;
    DeviceSelector m_selector = RunDevice;
};

} // namespace ProjectExplorer

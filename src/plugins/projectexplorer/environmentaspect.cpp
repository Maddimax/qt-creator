// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "environmentaspect.h"

#include "buildconfiguration.h"
#include "devicesupport/devicekitaspects.h"
#include "devicesupport/devicemanager.h"
#include "environmentaspectwidget.h"
#include "environmentwidget.h"
#include "environmentkitaspect.h"
#include "kit.h"
#include "kitmanager.h"
#include "projectexplorersettings.h"
#include "projectexplorertr.h"
#include "target.h"

#include <coreplugin/icore.h>

#include <utils/algorithm.h>
#include <utils/filedialogs.h>
#include <utils/guard.h>
#include <utils/qtcassert.h>

using namespace Utils;

namespace ProjectExplorer {

// EnvironmentItemsAspect

EnvironmentItemsAspect::EnvironmentItemsAspect(Utils::AspectContainer *container)
    : BaseAspect(container)
{}

Utils::AspectPresentation EnvironmentItemsAspect::presentation() const
{
    Utils::AspectPresentation p = BaseAspect::presentation();
    p.control = Utils::AspectControls::Table;
    return p;
}

QAbstractItemModel *EnvironmentItemsAspect::tableModel()
{
    return &m_model;
}

void EnvironmentItemsAspect::setCurrentRow(int row)
{
    if (m_currentRow == row)
        return;
    m_currentRow = row;
    emit currentRowChanged();
}

QModelIndex EnvironmentItemsAspect::currentIndex() const
{
    if (m_currentRow < 0 || m_currentRow >= m_model.rowCount({}))
        return {};
    return m_model.index(m_currentRow, 0);
}

// EnvironmentEditorAspect

EnvironmentEditorAspect::EnvironmentEditorAspect(Utils::AspectContainer *container)
    : Utils::AspectContainer(container)
{
    // Before registering: insertAspect() forces the container's own auto-apply
    // onto what it takes in.
    setAutoApply(true);

    m_variables.setQmlName("Variables");
    registerAspect(&m_variables);

    m_changes.setQmlName("Changes");
    m_changes.setLabelText(Tr::tr("Changes:"));
    m_changes.setDisplayStyle(Utils::StringAspect::TextEditDisplay);
    m_changes.setToolTip(Tr::tr("The changes to the environment, one per line, as NAME=VALUE."));
    registerAspect(&m_changes);

    const auto addAction = [this](Utils::ActionAspect &action,
                                  const QString &name,
                                  const QString &text,
                                  const std::function<void()> &run) {
        action.setQmlName(name);
        action.setActionText(text);
        action.setAction(run);
        registerAspect(&action);
    };

    addAction(m_edit, "Edit", Tr::tr("Ed&it"), [this] { editCurrent(); });
    addAction(m_add, "Add", Tr::tr("&Add"), [this] {
        m_variables.setCurrentRow(m_variables.model().addVariable().row());
    });
    addAction(m_reset, "Reset", Tr::tr("&Reset"), [this] {
        m_variables.model().resetVariable(currentName());
    });
    addAction(m_unset, "Unset", Tr::tr("&Unset"), [this] {
        // Unset what the base environment provides; remove what we added.
        Utils::EnvironmentModel &model = m_variables.model();
        const QString name = currentName();
        if (!model.canReset(name))
            model.resetVariable(name);
        else
            model.unsetVariable(name);
    });
    addAction(m_toggle, "Toggle", Tr::tr("Disable"), [this] {
        m_variables.model().toggleVariable(m_variables.currentIndex());
    });
    addAction(m_appendPath, "AppendPath", Tr::tr("Append Path..."), [this] {
        amendPathList(Utils::EnvironmentItem::Append);
    });
    addAction(m_prependPath, "PrependPath", Tr::tr("Prepend Path..."), [this] {
        amendPathList(Utils::EnvironmentItem::Prepend);
    });

    updateActions();

    connect(&m_variables.model(), &Utils::EnvironmentModel::userChangesChanged, this, [this] {
        if (m_updating.isLocked())
            return;
        const Utils::GuardLocker lock(m_updating);
        showChangesAsText();
        updateActions();
        emit changesEdited(m_variables.model().changes());
    });
    connect(&m_variables, &EnvironmentItemsAspect::currentRowChanged,
            this, [this] { updateActions(); });

    // The other surface: the same changes as text, and what is typed there is
    // what the table then shows.
    m_changes.addOnChanged(this, [this] {
        if (m_updating.isLocked())
            return;
        const Utils::GuardLocker lock(m_updating);
        Utils::EnvironmentChanges changes = m_variables.model().changes();
        changes.setItemsFromUser(
            Utils::EnvironmentItem::fromStringList(m_changes().split('\n', Qt::SkipEmptyParts)));
        m_variables.model().setUserChanges(changes);
        updateActions();
        emit changesEdited(changes);
    });
}

Utils::AspectPresentation EnvironmentEditorAspect::presentation() const
{
    Utils::AspectPresentation p = Utils::AspectContainer::presentation();
    p.control = Utils::AspectControls::EnvironmentEditor;
    return p;
}

void EnvironmentEditorAspect::setBaseEnvironment(const Utils::Environment &env)
{
    m_variables.model().setBaseEnvironment(env);
}

Utils::EnvironmentChanges EnvironmentEditorAspect::changes() const
{
    return const_cast<EnvironmentItemsAspect &>(m_variables).model().changes();
}

void EnvironmentEditorAspect::setChanges(const Utils::EnvironmentChanges &changes)
{
    const Utils::GuardLocker lock(m_updating);
    m_variables.model().setUserChanges(changes);
    showChangesAsText();
    updateActions();
}

QString EnvironmentEditorAspect::currentName() const
{
    auto &variables = const_cast<EnvironmentItemsAspect &>(m_variables);
    return variables.model().indexToVariable(variables.currentIndex());
}

void EnvironmentEditorAspect::showChangesAsText()
{
    m_changes.setValue(
        Utils::EnvironmentItem::toStringList(m_variables.model().changes().itemsFromUser())
            .join('\n'));
}

void EnvironmentEditorAspect::editCurrent()
{
    // A path list is edited in the dialog made for it; anything else is edited
    // in the cell, or in the text above.
    const QModelIndex current = m_variables.currentIndex();
    if (m_variables.model().currentEntryIsPathList(current))
        editEnvironmentPathList(&m_variables.model(), current, Core::ICore::dialogParent());
}

void EnvironmentEditorAspect::amendPathList(Utils::EnvironmentItem::Operation op)
{
    const QString name = currentName();
    const Utils::FilePath dir = Utils::FileUtils::getExistingDirectory(Tr::tr("Choose Directory"));
    if (dir.isEmpty())
        return;
    Utils::EnvironmentChanges changes = m_variables.model().changes();
    changes.appendUserItem({name, dir.toUserOutput(), op});
    m_variables.model().setUserChanges(changes);
}

void EnvironmentEditorAspect::updateActions()
{
    Utils::EnvironmentModel &model = m_variables.model();
    const QModelIndex current = m_variables.currentIndex();
    if (current.isValid()) {
        const QString name = model.indexToVariable(current);
        const bool modified = model.canReset(name) && model.hasExplicitChanges(name);
        const bool unset = model.isUnset(name);
        m_edit.setEnabled(true);
        m_reset.setEnabled(modified || unset);
        m_unset.setEnabled(!unset);
        m_toggle.setEnabled(!unset);
        m_toggle.setActionText(model.isEnabled(name) ? Tr::tr("Disable") : Tr::tr("Enable"));
    } else {
        m_edit.setEnabled(false);
        m_reset.setEnabled(false);
        m_unset.setEnabled(false);
        m_toggle.setEnabled(false);
        m_toggle.setActionText(Tr::tr("Disable"));
    }
    const bool isPathList = model.currentEntryIsPathList(current);
    m_appendPath.setEnabled(isPathList);
    m_prependPath.setEnabled(isPathList);
}


const char PRINT_ON_RUN_KEY[] = "PE.EnvironmentAspect.PrintOnRun";

EnvironmentAspect::EnvironmentAspect(AspectContainer *container)
    : BaseAspect(container)
{
    setDisplayName(Tr::tr("Environment"));
    setId("EnvironmentAspect");
    setConfigWidgetCreator([this] { return new EnvironmentAspectWidget(this); });
    addDataExtractor(this, &EnvironmentAspect::environment, &Data::environment);
    if (const auto runConfig = qobject_cast<RunConfiguration *>(container)) {
        addModifier([runConfig](Environment &env) {
            ProjectExplorerSettings::get(runConfig)
                .appEnvChanges()
                .modifyEnvironment(env, runConfig->macroExpander());
            EnvironmentKitAspect::runEnvChanges(runConfig->kit())
                .modifyEnvironment(env, runConfig->macroExpander());
        });
        globalProjectExplorerSettings().appEnvChanges.addOnChanged(this, [this] {
            emit environmentChanged();
        });
    }
    connect(this, &EnvironmentAspect::environmentChanged, this, &BaseAspect::changed);
}

void EnvironmentAspect::setDeviceSelector(Kit *kit, DeviceSelector selector)
{
    QTC_ASSERT(!m_kit, return);
    QTC_ASSERT(kit, return);

    m_kit = kit;
    m_selector = selector;

    handleKitUpdate();
    connect(KitManager::instance(), &KitManager::kitUpdated, this, [this](Kit *k) {
        if (k == m_kit) {
            handleKitUpdate();
            emit devicePotentiallyChanged();
        }
    });
    emit devicePotentiallyChanged();
}

int EnvironmentAspect::baseEnvironmentBase() const
{
    return m_base;
}

void EnvironmentAspect::setBaseEnvironmentBase(int base)
{
    QTC_ASSERT(base >= 0 && base < m_baseEnvironments.size(), return);
    if (m_base != base) {
        m_base = base;
        emit baseEnvironmentChanged();
    }
}

void EnvironmentAspect::setUserEnvironmentChanges(const EnvironmentChanges &diff)
{
    if (m_userChanges != diff) {
        m_userChanges = diff;
        emit userEnvironmentChangesChanged(m_userChanges);
        emit environmentChanged();
    }
}

Environment EnvironmentAspect::environment() const
{
    Environment env = modifiedBaseEnvironment();
    userEnvironmentChanges().modifyEnvironment(env, macroExpander());
    return env;
}

Environment EnvironmentAspect::expandedEnvironment(const MacroExpander &expander) const
{
    Environment expandedEnv;
    environment().forEachEntry([&](const QString &key, const QString &value, bool enabled) {
        expandedEnv.set(key, expander.expand(value), enabled);
    });
    return expandedEnv;
}

Environment EnvironmentAspect::modifiedBaseEnvironment() const
{
    QTC_ASSERT(m_base >= 0 && m_base < m_baseEnvironments.size(), return Environment());
    Environment env = m_baseEnvironments.at(m_base).unmodifiedBaseEnvironment();
    for (const EnvironmentModifier &modifier : m_modifiers)
        modifier(env);
    return env;
}

const QStringList EnvironmentAspect::displayNames() const
{
    return Utils::transform(m_baseEnvironments, &BaseEnvironment::displayName);
}

void EnvironmentAspect::addModifier(const EnvironmentAspect::EnvironmentModifier &modifier)
{
    m_modifiers.append(modifier);
}

IDeviceConstPtr EnvironmentAspect::device() const
{
    switch (m_selector) {
    case BuildDevice:
        return BuildDeviceKitAspect::device(m_kit);
    case RunDevice:
        return RunDeviceKitAspect::device(m_kit);
    case HostDevice:
        return DeviceManager::defaultDesktopDevice();
    }
    return {};
}

int EnvironmentAspect::addSupportedBaseEnvironment(const QString &displayName,
                                                   const std::function<Environment()> &getter)
{
    BaseEnvironment baseEnv;
    baseEnv.displayName = displayName;
    baseEnv.getter = getter;
    m_baseEnvironments.append(baseEnv);
    const int index = m_baseEnvironments.size() - 1;
    if (m_base == -1)
        setBaseEnvironmentBase(index);

    return index;
}

int EnvironmentAspect::addPreferredBaseEnvironment(const QString &displayName,
                                                   const std::function<Environment()> &getter)
{
    BaseEnvironment baseEnv;
    baseEnv.displayName = displayName;
    baseEnv.getter = getter;
    m_baseEnvironments.append(baseEnv);
    const int index = m_baseEnvironments.size() - 1;
    setBaseEnvironmentBase(index);

    return index;
}

void EnvironmentAspect::setSupportForBuildEnvironment(BuildConfiguration *bc)
{
    setIsLocal(true);
    addSupportedBaseEnvironment(Tr::tr("Clean Environment"), {});

    addSupportedBaseEnvironment(Tr::tr("System Environment"), [] {
        return Environment::systemEnvironment();
    });
    addPreferredBaseEnvironment(Tr::tr("Build Environment"), [bc] { return bc->environment(); });

    connect(bc, &BuildConfiguration::environmentChanged,
            this, &EnvironmentAspect::environmentChanged);
}

void EnvironmentAspect::fromMap(const Store &map)
{
    m_base = map.value(BASE_KEY, -1).toInt();
    m_userChanges = EnvironmentChanges::createFromVariant(map.value(CHANGES_KEY));
    m_printOnRun = map.value(PRINT_ON_RUN_KEY).toBool();
}

void EnvironmentAspect::toMap(Store &data) const
{
    data.insert(BASE_KEY, m_base);
    data.insert(CHANGES_KEY, m_userChanges.toVariant());
    data.insert(PRINT_ON_RUN_KEY, m_printOnRun);
}

QString EnvironmentAspect::currentDisplayName() const
{
    QTC_ASSERT(m_base >= 0 && m_base < m_baseEnvironments.size(), return {});
    return m_baseEnvironments[m_base].displayName;
}

Environment EnvironmentAspect::BaseEnvironment::unmodifiedBaseEnvironment() const
{
    return getter ? getter() : Environment();
}

EnvironmentChanges EnvironmentAspect::userEnvironmentChanges() const
{
    emit userChangesUpdateRequested();
    return m_userChanges;
}

} // namespace ProjectExplorer

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "environmentkitaspect.h"

#include "devicesupport/devicekitaspects.h"
#include "devicesupport/idevice.h"
#include "projectexplorertr.h"
#include "kit.h"
#include "kitaspect.h"
#include "kitmanager.h"
#include "toolchain.h"

#include <coreplugin/icore.h>

#include <utils/aspectwidgets.h>
#include <utils/environment.h>
#include <utils/environmentdialog.h>
#include <utils/guard.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/variablechooser.h>

using namespace Utils;

namespace ProjectExplorer {

static EnvironmentItem forceMSVCEnglishItem()
{
    static EnvironmentItem item("VSLANG", "1033");
    return item;
}

static bool enforcesMSVCEnglish(const EnvironmentChanges &changes)
{
    return changes.itemsFromUser().contains(forceMSVCEnglishItem())
        || changes.itemsFromFile().contains(forceMSVCEnglishItem());
}

static Id buildEnvId() { return "PE.Profile.Environment"; }
static Id runEnvId() { return "PE.Profile.RunEnvironment"; }

namespace Internal {
class EnvironmentKitAspectImpl final : public KitAspect
{
public:
    EnvironmentKitAspectImpl(Kit *workingCopy, const KitAspectFactory *factory)
        : KitAspect(workingCopy, factory)
    {
        if (HostOsInfo::isWindowsHost())
            initMSVCOutputSwitch();

        // What each button does is open a dialog, which is not a change the
        // page is dirty about until the dialog says something changed.
        m_buildEnv = addControl<Utils::ActionAspect>();
        m_buildEnv->setActionText(Tr::tr("Edit Build Environment..."));
        m_buildEnv->setAction([this] { editBuildEnvironmentChanges(); });
        setIgnoreForDirtyHook(m_buildEnv);

        m_runEnv = addControl<Utils::ActionAspect>();
        m_runEnv->setActionText(Tr::tr("Edit Run Environment..."));
        m_runEnv->setAction([this] { editRunEnvironmentChanges(); });
        setIgnoreForDirtyHook(m_runEnv);

        valueToVolatileValue();
        refresh();
    }

private:
    void makeReadOnly(bool readOnly) override
    {
        if (m_forceUtf8)
            m_forceUtf8->setEnabled(!readOnly);
        m_buildEnv->setEnabled(!readOnly);
        m_runEnv->setEnabled(!readOnly);
    }

    void refresh() override
    {
        // What the changes come to is what the button says when hovered.
        m_buildEnv->setToolTip(m_buildEnvChanges.toShortSummary(kit()->macroExpander(), true));
        m_runEnv->setToolTip(m_runEnvChanges.toShortSummary(kit()->macroExpander(), true));

        if (m_forceUtf8) {
            // Writing the box is not the user ticking it.
            const GuardLocker locker(m_ignoreChanges);
            m_forceUtf8->setValue(enforcesMSVCEnglish(m_buildEnvChanges));
        }

        // TODO: Set an icon on the button representing whether there are changes or not.
    }

    bool isDirty() const final
    {
        return m_buildEnvChanges != EnvironmentKitAspect::buildEnvChanges(kit())
            || m_runEnvChanges != EnvironmentKitAspect::runEnvChanges(kit());
    }

    bool valueToVolatileValue() final
    {
        bool res1 = updateStorage(m_buildEnvChanges, EnvironmentKitAspect::buildEnvChanges(kit()));
        bool res2 = updateStorage(m_runEnvChanges, EnvironmentKitAspect::runEnvChanges(kit()));
        return res1 || res2;
    }

    bool volatileValueToValue() final
    {
        bool changed = false;
        if (m_buildEnvChanges != EnvironmentKitAspect::buildEnvChanges(kit())) {
            EnvironmentKitAspect::setBuildEnvChanges(kit(), m_buildEnvChanges);
            changed = true;
        }
        if (m_runEnvChanges != EnvironmentKitAspect::runEnvChanges(kit())) {
            EnvironmentKitAspect::setRunEnvChanges(kit(), m_runEnvChanges);
            changed = true;
        }
        return changed;
    }

    void editBuildEnvironmentChanges()
    {
        FilePath browseHint;
        if (const IDeviceConstPtr &device = BuildDeviceKitAspect::device(kit()))
            browseHint = device->rootPath();
        std::optional<EnvironmentChanges> changes = runEnvironmentItemsDialog(
            Core::ICore::dialogParent(),
            m_buildEnvChanges,
            QString(),
            polisher(),
            Tr::tr("Edit Build Environment"),
            browseHint);
        if (!changes)
            return;

        if (m_forceUtf8) {
            // re-add what envWithoutMSVCEnglishEnforcement removed
            // or update the box if the user added it by hand
            if (m_forceUtf8->volatileValue() && !enforcesMSVCEnglish(*changes)) {
                changes->appendUserItem(forceMSVCEnglishItem());
            } else if (enforcesMSVCEnglish(*changes)) {
                const GuardLocker locker(m_ignoreChanges);
                m_forceUtf8->setValue(true);
            }
        }

        if (updateStorage(m_buildEnvChanges, *changes)) {
            emit volatileValueChanged();
            refresh();
            markSettingsDirty();
        }
    }

    void editRunEnvironmentChanges()
    {
        FilePath browseHint;
        if (const IDeviceConstPtr &device = RunDeviceKitAspect::device(kit()))
            browseHint = device->rootPath();
        const std::optional<EnvironmentChanges> changes = runEnvironmentItemsDialog(
            Core::ICore::dialogParent(),
            m_runEnvChanges,
            QString(),
            polisher(),
            Tr::tr("Edit Run Environment"),
            browseHint);
        if (!changes)
            return;

        if (updateStorage(m_runEnvChanges, *changes)) {
            emit volatileValueChanged();
            refresh();
            markSettingsDirty();
        }
    }

    NameValuesDialog::Polisher polisher() const
    {
        return [expander = kit()->macroExpander()](QWidget *w) {
            VariableChooser::addSupportForChildWidgets(w, {w, expander}); // FIXME: Use better guard?
        };
    }

    void initMSVCOutputSwitch()
    {
        m_forceUtf8 = addControl<Utils::BoolAspect>();
        m_forceUtf8->setLabelText(Tr::tr("Force UTF-8 MSVC output"));
        m_forceUtf8->setLabelPlacement(Utils::BoolAspect::LabelPlacement::AtCheckBox);
        m_forceUtf8->setToolTip(Tr::tr("Either switches MSVC to English or keeps the language and "
                                       "just forces UTF-8 output (may vary depending on the used MSVC "
                                       "compiler)."));
        m_forceUtf8->setValue(enforcesMSVCEnglish(m_buildEnvChanges));
        m_forceUtf8->addOnVolatileValueChanged(this, [this] {
            if (m_ignoreChanges.isLocked())
                return;
            const bool checked = m_forceUtf8->volatileValue();
            const bool hasVsLangEntry = enforcesMSVCEnglish(m_buildEnvChanges);
            if (checked && !hasVsLangEntry)
                m_buildEnvChanges.appendUserItem(forceMSVCEnglishItem());
            else if (!checked && hasVsLangEntry)
                m_buildEnvChanges.removeUserItem(forceMSVCEnglishItem());
            if (checked != hasVsLangEntry)
                emit volatileValueChanged();
            refresh();
        });
    }

    Utils::ActionAspect *m_buildEnv = nullptr;
    Utils::ActionAspect *m_runEnv = nullptr;
    Utils::BoolAspect *m_forceUtf8 = nullptr;
    Guard m_ignoreChanges;

    // Used as "volatile value" of the aspect.
    EnvironmentChanges m_runEnvChanges;
    EnvironmentChanges m_buildEnvChanges;
};

class EnvironmentKitAspectFactory : public KitAspectFactory
{
public:
    EnvironmentKitAspectFactory();

    Tasks validate(const Kit *) const override { return {}; }
    void addToBuildEnvironment(const Kit *k, Environment &env) const override;
    void addToRunEnvironment(const Kit *, Environment &) const override;

    KitAspect *createKitAspect(Kit *k) const override;

    ItemList toUserOutput(const Kit *k) const override;
};

EnvironmentKitAspectFactory::EnvironmentKitAspectFactory()
{
    setId(EnvironmentKitAspect::id());
    setDisplayName(Tr::tr("Environment"));
    setDescription(Tr::tr("Additional build environment settings when using this kit."));
    setPriority(29000);
}

void EnvironmentKitAspectFactory::addToBuildEnvironment(const Kit *k, Environment &env) const
{
    EnvironmentKitAspect::buildEnvChanges(k).modifyEnvironment(env, k->macroExpander());
}

void EnvironmentKitAspectFactory::addToRunEnvironment(const Kit *k, Environment &env) const
{
    EnvironmentKitAspect::runEnvChanges(k).modifyEnvironment(env, k->macroExpander());
}

KitAspect *EnvironmentKitAspectFactory::createKitAspect(Kit *k) const
{
    QTC_ASSERT(k, return nullptr);
    return new Internal::EnvironmentKitAspectImpl(k, this);
}

KitAspectFactory::ItemList EnvironmentKitAspectFactory::toUserOutput(const Kit *k) const
{
    ItemList list;
    const auto addIfNotEmpty = [&](const QString &displayName,
                                   const EnvironmentChanges &changes) {
        if (changes.hasItems()) {
            Environment env;
            changes.modifyEnvironment(env, k->macroExpander());
            list.emplaceBack(displayName, env.toStringList().join("<br>"));
        }
    };
    addIfNotEmpty(Tr::tr("Build Environment"), EnvironmentKitAspect::buildEnvChanges(k));
    addIfNotEmpty(Tr::tr("Run Environment"), EnvironmentKitAspect::runEnvChanges(k));
    return list;
}

const EnvironmentKitAspectFactory theEnvironmentKitAspectFactory;

} // namespace Internal

Id EnvironmentKitAspect::id()
{
    return buildEnvId();
}

EnvironmentChanges EnvironmentKitAspect::buildEnvChanges(const Kit *k)
{
    if (k)
        return EnvironmentChanges::createFromVariant(k->value(buildEnvId()));
    return {};
}

void EnvironmentKitAspect::setBuildEnvChanges(Kit *k, const EnvironmentChanges &changes)
{
    if (k)
        k->setValue(buildEnvId(), changes.toVariant());
}

EnvironmentChanges EnvironmentKitAspect::runEnvChanges(const Kit *k)
{
    if (k)
        return EnvironmentChanges::createFromVariant(k->value(runEnvId()));
    return {};
}

void EnvironmentKitAspect::setRunEnvChanges(Kit *k, const EnvironmentChanges &changes)
{
    if (k)
        k->setValue(runEnvId(), changes.toVariant());
}

} // namespace ProjectExplorer

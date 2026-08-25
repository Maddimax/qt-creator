// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "mcusupportoptionspage.h"
#include "mcukitmanager.h"
#include "mcupackage.h"
#include "mcusupportconstants.h"
#include "mcusupportoptions.h"
#include "mcusupportsdk.h"
#include "mcusupporttr.h"
#include "mcutarget.h"
#include "settingshandler.h"

#include <cmakeprojectmanager/cmakeprojectconstants.h>
#include <cmakeprojectmanager/cmaketoolmanager.h>
#include <coreplugin/icore.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <utils/algorithm.h>
#include <utils/guiutils.h>
#include <utils/aspects.h>
#include <utils/shutdownguard.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace McuSupport::Internal {

class McuSupportOptionsWidget final : public Utils::AspectContainer
{
public:
    McuSupportOptionsWidget(McuSupportOptions &, const SettingsHandler::Ptr &);

    void apply() final;

private:
    void updateStatus();
    void showMcuTargetPackages();
    void populateMcuTargetsComboBox();
    [[nodiscard]] McuTargetPtr currentMcuTarget() const;

    McuSupportOptions &m_options;
    SettingsHandler::Ptr m_settingsHandler;

    // Why the page is empty, when it is. Shown only when there is no CMake.
    Utils::TextDisplay m_status{this};
    Utils::AspectContainer m_sdkGroup{this};
    Utils::AspectContainer m_targetsGroup{this};
    Utils::SelectionAspect m_target{&m_targetsGroup};
    // What the selected target asks for, which is a different list per target.
    Utils::AspectContainer m_packagesGroup{this};
    Utils::AspectContainer m_optionalPackagesGroup{this};
    Utils::TextDisplay m_targetsInfo{this};
    Utils::BoolAspect m_automaticKitCreation{this};
    Utils::AspectContainer m_kitGroup{this};
    Utils::TextDisplay m_kitInfo{&m_kitGroup};
    Utils::ActionAspect m_createKit{&m_kitGroup};
    Utils::ActionAspect m_updateKit{&m_kitGroup};
};

McuSupportOptionsWidget::McuSupportOptionsWidget(McuSupportOptions &options,
                                                 const SettingsHandler::Ptr &settingsHandler)
    : m_options{options}
    , m_settingsHandler(settingsHandler)
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/McuSupport/McuSupportPage.qml"));

    m_status.setQmlName("Status");
    m_status.setIconType(Utils::InfoType::NotOk);
    m_status.setTextFormat(Utils::AspectControls::TextFormat::RichText);
    m_status.setText(Tr::tr("No CMake tool was detected. Add a CMake tool in the "
                            "<a href=\"cmake\">CMake options</a> and select Apply."));
    connect(&m_status, &Utils::TextDisplay::linkActivated, this, [] {
        Core::ICore::showSettings(CMakeProjectManager::Constants::Settings::TOOLS_ID);
    });

    m_sdkGroup.setQmlName("SdkGroup");
    m_sdkGroup.setLabelText(Tr::tr("Qt for MCUs SDK"));
    // Re-read the qtForMCUs package from settings to discard un-applied
    // changes from previous sessions.
    m_options.qtForMCUsSdkPackage->readFromSettings();
    m_options.qtForMCUsSdkPackage->addSettingsRows(m_sdkGroup);

    m_targetsGroup.setQmlName("TargetsGroup");
    m_targetsGroup.setLabelText(
        Tr::tr("Targets supported by the %1").arg(m_sdkGroup.labelText()));
    m_target.setQmlName("Target");
    m_target.setDisplayStyle(Utils::SelectionAspect::DisplayStyle::ComboBox);

    m_packagesGroup.setQmlName("PackagesGroup");
    m_packagesGroup.setLabelText(Tr::tr("Requirements"));

    m_optionalPackagesGroup.setQmlName("OptionalPackagesGroup");
    m_optionalPackagesGroup.setLabelText(Tr::tr("Optional"));

    m_targetsInfo.setQmlName("TargetsInfo");
    m_targetsInfo.setIconType(Utils::InfoType::NotOk);

    m_automaticKitCreation.setQmlName("AutomaticKitCreation");
    m_automaticKitCreation.setLabelText(
        Tr::tr("Automatically create kits for all available targets on start"));
    m_automaticKitCreation.setLabelPlacement(Utils::BoolAspect::LabelPlacement::Compact);
    m_automaticKitCreation.setValue(m_options.automaticKitCreationEnabled());

    // The message and the two buttons read as one line, which is what the
    // horizontal layout in the group used to say.
    m_kitGroup.setQmlName("KitGroup");
    m_kitGroup.setLabelText(Tr::tr("Create a Kit"));
    m_kitGroup.setInlineRow(true);
    m_kitInfo.setQmlName("KitInfo");
    m_createKit.setQmlName("CreateKit");
    m_createKit.setActionText(Tr::tr("Create Kit"));
    m_createKit.setAction([this] {
        McuKitManager::newKit(currentMcuTarget().get(), m_options.qtForMCUsSdkPackage);
        m_options.registerQchFiles();
        updateStatus();
    });
    m_updateKit.setQmlName("UpdateKit");
    m_updateKit.setActionText(Tr::tr("Update Kit"));
    m_updateKit.setAction([this] {
        for (auto *kit : McuKitManager::upgradeableKits(currentMcuTarget().get(),
                                                        m_options.qtForMCUsSdkPackage))
            McuKitManager::upgradeKitInPlace(kit, currentMcuTarget().get(),
                                             m_options.qtForMCUsSdkPackage);
        updateStatus();
    });

    // Behaviour, not layout.
    connect(&m_target, &Utils::SelectionAspect::volatileValueChanged,
            this, &McuSupportOptionsWidget::showMcuTargetPackages);
    connect(m_options.qtForMCUsSdkPackage.get(), &McuAbstractPackage::changed,
            this, &McuSupportOptionsWidget::populateMcuTargetsComboBox);
    connect(&m_options, &McuSupportOptions::packagesChanged,
            this, &McuSupportOptionsWidget::updateStatus);
    // Asking the SDK what it has costs something, so it waits until the page
    // is actually looked at rather than running for every page census.
    connect(this, &Utils::AspectContainer::shown,
            this, &McuSupportOptionsWidget::populateMcuTargetsComboBox);

    showMcuTargetPackages();
}

void McuSupportOptionsWidget::updateStatus()
{
    const McuTargetPtr mcuTarget = currentMcuTarget();

    const bool cMakeAvailable = !CMakeProjectManager::CMakeToolManager::cmakeTools().isEmpty();

    // Page elements
    {
        m_sdkGroup.setVisible(cMakeAvailable);
        const bool valid = cMakeAvailable && m_options.qtForMCUsSdkPackage->isValidStatus();
        const bool ready = valid && mcuTarget;
        m_targetsGroup.setVisible(ready);
        m_packagesGroup.setVisible(ready && !mcuTarget->packages().isEmpty());
        m_optionalPackagesGroup.setVisible(
            ready && Utils::anyOf(mcuTarget->packages(), [](McuPackagePtr p) {
                return p->isOptional();
            }));
        m_kitGroup.setVisible(ready);
        m_targetsInfo.setVisible(valid && m_options.sdkRepository.mcuTargets.isEmpty());
        if (m_targetsInfo.isVisible()) {
            const Utils::FilePath sdkPath = m_options.qtForMCUsSdkPackage->basePath();
            QString deprecationMessage;
            if (checkDeprecatedSdkError(sdkPath, deprecationMessage))
                m_targetsInfo.setText(deprecationMessage);
            else
                m_targetsInfo.setText(Tr::tr("No valid kit descriptions found at %1.")
                                          .arg(kitsPath(sdkPath).toUserOutput()));
        }
    }

    // Kit creation status
    if (mcuTarget) {
        const bool mcuTargetValid = mcuTarget->isValid();
        m_createKit.setVisible(mcuTargetValid);
        m_updateKit.setVisible(mcuTargetValid);
        if (mcuTargetValid) {
            const bool hasMatchingKits = !McuKitManager::matchingKits(mcuTarget.get(),
                                                                      m_options.qtForMCUsSdkPackage)
                                              .isEmpty();
            const bool hasUpgradeableKits
                = !hasMatchingKits
                  && !McuKitManager::upgradeableKits(mcuTarget.get(), m_options.qtForMCUsSdkPackage)
                          .isEmpty();

            m_createKit.setEnabled(!hasMatchingKits);
            m_updateKit.setEnabled(hasUpgradeableKits);

            m_kitInfo.setIconType(!hasMatchingKits ? Utils::InfoType::Information
                                                   : Utils::InfoType::Ok);
            m_kitInfo.setText(
                hasMatchingKits
                    ? Tr::tr("A kit for the selected target and SDK version already exists.")
                : hasUpgradeableKits ? Tr::tr("Kits for a different SDK version exist.")
                                     : Tr::tr("A kit for the selected target can be created."));
        } else {
            m_kitInfo.setIconType(Utils::InfoType::NotOk);
            m_kitInfo.setText(Tr::tr("Provide the package paths to create a kit "
                                     "for your target."));
        }
    }

    // Automatic Kit creation
    m_automaticKitCreation.setValue(m_options.automaticKitCreationEnabled());

    // Status label in the bottom
    m_status.setVisible(!cMakeAvailable);
}

struct McuPackageSort {
    bool operator()(McuPackagePtr a, McuPackagePtr b) const {
        if (a->cmakeVariableName() != b->cmakeVariableName())
            return a->cmakeVariableName() > b->cmakeVariableName();
        else
            return a->environmentVariableName() > b->environmentVariableName();
    }
};

void McuSupportOptionsWidget::showMcuTargetPackages()
{
    McuTargetPtr mcuTarget = currentMcuTarget();
    if (!mcuTarget)
        return;

    m_packagesGroup.clear();
    m_optionalPackagesGroup.clear();

    std::set<McuPackagePtr, McuPackageSort> packages;

    for (const auto &package : mcuTarget->packages()) {
        if (package->label().isEmpty())
            continue;
        packages.insert(package);
    }

    const MacroExpanderPtr macroExpander = m_options.sdkRepository.getMacroExpander(*mcuTarget);
    for (const auto &package : packages) {
        // What Reset goes back to is the default with this target's macros in
        // it, which only the page can work out.
        package->setExpandedDefaultPath(macroExpander->expand(package->defaultPath()));
        package->addSettingsRows(package->isOptional() ? m_optionalPackagesGroup
                                                       : m_packagesGroup);
    }

    updateStatus();
}

McuTargetPtr McuSupportOptionsWidget::currentMcuTarget() const
{
    const int mcuTargetIndex = m_target.volatileValue();
    McuTargetPtr target{nullptr};
    if (mcuTargetIndex != -1 && mcuTargetIndex < m_options.sdkRepository.mcuTargets.size())
        target = m_options.sdkRepository.mcuTargets.at(mcuTargetIndex);

    return target;
}

void McuSupportOptionsWidget::populateMcuTargetsComboBox()
{
    m_options.populatePackagesAndTargets();
    int initialPlatformIndex = 0;
    m_target.clearOptions();
    for (const McuTargetPtr &t : m_options.sdkRepository.mcuTargets) {
        if (t->platform().name == m_settingsHandler->initialPlatformName())
            initialPlatformIndex = m_options.sdkRepository.mcuTargets.indexOf(t);
        m_target.addOption(McuKitManager::generateKitNameFromTarget(t.get()));
    }
    if (!m_options.sdkRepository.mcuTargets.isEmpty())
        m_target.setValue(initialPlatformIndex);
    showMcuTargetPackages();
}

void McuSupportOptionsWidget::apply()
{
    m_options.setAutomaticKitCreationEnabled(m_automaticKitCreation.volatileValue());

    bool pathsChanged = false;

    m_settingsHandler->setAutomaticKitCreation(m_options.automaticKitCreationEnabled());
    m_options.sdkRepository.expandVariablesAndWildcards();

    if (m_options.sdkRepository.mcuTargets.isEmpty())
        return;

    const auto warn = [](const QString &detail) {
        QMessageBox warningPopup(QMessageBox::Icon::Warning,
                                 Tr::tr("Warning"),
                                 Tr::tr("Cannot apply changes in SDKs > MCU."),
                                 QMessageBox::Ok,
                                 Core::ICore::dialogParent());
        warningPopup.setInformativeText(detail);
        warningPopup.exec();
    };

    auto target = currentMcuTarget();
    if (!target) {
        warn(Tr::tr("No target selected."));
        return;
    }
    if (!target->isValid()) {
        warn(Tr::tr("Invalid paths present for target\n%1")
                 .arg(McuKitManager::generateKitNameFromTarget(target.get())));
        return;
    }

    pathsChanged |= m_options.qtForMCUsSdkPackage->writeToSettings();
    for (const auto &package : target->packages())
        pathsChanged |= package->writeToSettings();

    if (pathsChanged) {
        m_options.checkUpgradeableKits();
        McuKitManager::updatePathsInExistingKits(m_settingsHandler);
    }
}

McuSupportOptionsPage::McuSupportOptionsPage(McuSupportOptions &options,
                                             const SettingsHandler::Ptr &settingsHandler)
{
    setId(Utils::Id(Constants::SETTINGS_ID));
    setDisplayName(Tr::tr("MCU"));
    setCategory(ProjectExplorer::Constants::SDK_SETTINGS_CATEGORY);
    setSettingsProvider([&options, &settingsHandler] {
        static Utils::GuardedObject<McuSupportOptionsWidget> theAspects(options, settingsHandler);
        return theAspects.get();
    });
}

} // namespace McuSupport::Internal

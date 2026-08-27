// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "toolchainoptionspage.h"

#include "abi.h"
#include "devicesupport/devicemanager.h"
#include "devicesupport/deviceselectionaspect.h"
#include "kitaspect.h"
#include "projectexplorerconstants.h"
#include "projectexplorertr.h"
#include "toolchain.h"
#include "toolchainconfigaspects.h"
#include "toolchainmanager.h"

#include <coreplugin/icore.h>

#include <utils/algorithm.h>
#include <utils/detailswidget.h>
#include <utils/groupedlistaspect.h>
#include <utils/groupedmodel.h>
#include <utils/guard.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>
#include <utils/shutdownguard.h>
#include <utils/treemodel.h>
#include <utils/utilsicons.h>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace ProjectExplorer::Internal {

QVariant toolchainBundleData(const std::optional<ToolchainBundle> &bundle, int column, int role)
{
    switch (role) {
    case Qt::DisplayRole:
        if (column == 0)
            return bundle ? bundle->displayName() : Tr::tr("None", "Toolchain bundle display name");
        if (!bundle)
            return {};
        if (column == 1)
            return bundle->typeDisplayName();
        if (column == 2 && bundle->factory())
            return ToolchainManager::displayNameOfLanguageCategory(bundle->factory()->languageCategory());
        return {};
    case Qt::ToolTipRole: {
        if (!bundle)
            return {};
        QString toolTip;
        const ToolchainBundle::Valid validity = bundle->validity();
        if (validity != ToolchainBundle::Valid::None) {
            toolTip = "<nobr>" + Tr::tr("<b>ABI:</b> %1").arg(bundle->targetAbi().toString());
            if (validity == ToolchainBundle::Valid::Some)
                toolTip.append("<br/>").append(Tr::tr("Not all compilers are set up correctly."));
        } else {
            toolTip = Tr::tr("This toolchain is invalid.");
        }
        return QVariant("<div style=\"white-space:pre\">" + toolTip + "</div>");
    }
    case Qt::DecorationRole:
        if (!bundle)
            return {};
        if (column == 0) {
            switch (bundle->validity()) {
            case ToolchainBundle::Valid::All:
                break;
            case ToolchainBundle::Valid::Some:
                return Utils::Icons::WARNING.icon();
            case ToolchainBundle::Valid::None:
                return Utils::Icons::CRITICAL.icon();
            }
        }
        return QVariant();
    case KitAspect::IdRole:
        return bundle ? bundle->bundleId().toSetting() : QVariant();
    case KitAspect::IsNoneRole:
        return !bundle;
    case KitAspect::TypeRole:
        return bundle ? bundle->typeDisplayName() : QString();
    case KitAspect::QualityRole:
        return bundle ? int(bundle->validity()) : -1;
    case FilePathRole:
        return bundle && bundle->validity() != ToolchainBundle::Valid::None
            ? bundle->get(&Toolchain::compilerCommand).toVariant()
            : QVariant();
    }
    return {};
}

struct ToolchainTreeItem
{
    ToolchainTreeItem() = default;
    explicit ToolchainTreeItem(const ToolchainBundle &b)
        : bundle(b) {}

    friend bool operator==(const ToolchainTreeItem &a, const ToolchainTreeItem &b)
    {
        return a.bundle == b.bundle;
    }

    std::optional<ToolchainBundle> bundle;
};

class DetectionSettingsDialog : public QDialog
{
public:
    DetectionSettingsDialog(const ToolchainDetectionSettings &settings, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(Tr::tr("Toolchain Auto-detection Settings"));
        const auto layout = new QVBoxLayout(this);
        m_detectX64AsX32CheckBox.setText(Tr::tr("Detect x86_64 GCC compilers "
                                                "as x86_64 and x86"));
        m_detectX64AsX32CheckBox.setToolTip(
            Tr::tr("If checked, %1 will "
                   "set up two instances of each x86_64 compiler:\nOne for the native x86_64 "
                   "target, and "
                   "one for a plain x86 target.\nEnable this if you plan to create 32-bit x86 "
                   "binaries "
                   "without using a dedicated cross compiler.")
                .arg(QGuiApplication::applicationDisplayName()));
        m_detectX64AsX32CheckBox.setChecked(settings.detectX64AsX32);
        layout->addWidget(&m_detectX64AsX32CheckBox);
        const auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttonBox);
    }

    ToolchainDetectionSettings settings() const
    {
        ToolchainDetectionSettings s;
        s.detectX64AsX32 = m_detectX64AsX32CheckBox.isChecked();
        return s;
    }

private:
    QCheckBox m_detectX64AsX32CheckBox;
};

} // namespace ProjectExplorer::Internal

Q_DECLARE_METATYPE(ProjectExplorer::Internal::ToolchainTreeItem)

namespace ProjectExplorer::Internal {

class ToolchainModel final : public TypedGroupedModel<ToolchainTreeItem>
{
public:
    ToolchainModel();
    ~ToolchainModel() override;

    int insertBundle(const ToolchainBundle &bundle, bool changed = false);
    int addBundle(const ToolchainBundle &bundle);
    int cloneRow(int row) override;
    // What the kind of toolchain in this row asks to be configured with, made
    // on first look and kept: a page that came back to a row would otherwise
    // lose what was typed into it.
    ToolchainConfigAspects *configAspects(int row);
    int rowForBundleId(const Id &id) const;
    void markRemoved(int row) override;
    void destroyBundle(int row);
    void apply() override;

    Guard m_registerGuard;
    Guard m_deregisterGuard;

private:
    QVariant variantData(int row, int column, int role) const override;

    QMap<Id, ToolchainConfigAspects *> m_configAspects;
};

ToolchainModel::ToolchainModel()
{
    setHeader({Tr::tr("Name"), Tr::tr("Type"), Tr::tr("Language")});
    setFilters(Constants::msgAutoDetected(), {{Constants::msgManual(), [this](int row) {
        const ToolchainTreeItem it = item(row);
        return it.bundle && !it.bundle->detectionSource().isAutoDetected();
    }}});
}

ToolchainModel::~ToolchainModel()
{
    for (int row = 0; row < itemCount(); ++row) {
        if (isAdded(row)) {
            ToolchainTreeItem it = item(row);
            if (it.bundle)
                it.bundle->deleteToolchains();
        }
    }
    qDeleteAll(m_configAspects);
}

int ToolchainModel::insertBundle(const ToolchainBundle &bundle, bool changed)
{
    const int row = appendItem(ToolchainTreeItem{bundle});
    if (changed)
        setChanged(row, true);
    return row;
}

int ToolchainModel::addBundle(const ToolchainBundle &bundle)
{
    return appendVolatileItem(ToolchainTreeItem{bundle});
}

int ToolchainModel::cloneRow(int row)
{
    const ToolchainTreeItem it = item(row);
    if (!it.bundle || it.bundle->validity() == ToolchainBundle::Valid::None)
        return -1;
    ToolchainBundle bundle = it.bundle->clone();
    bundle.setDetectionSource(DetectionSource::Manual);
    bundle.setDisplayName(Tr::tr("Clone of %1").arg(it.bundle->displayName()));
    return addBundle(bundle);
}

ToolchainConfigAspects *ToolchainModel::configAspects(int row)
{
    const ToolchainTreeItem it = item(row);
    if (!it.bundle || !it.bundle->factory())
        return nullptr;
    const Id bundleId = it.bundle->bundleId();
    ToolchainConfigAspects *&aspects = m_configAspects[bundleId];
    if (!aspects) {
        aspects = it.bundle->factory()->createConfigurationAspects(*it.bundle).release();
        if (aspects) {
            aspects->setAutoApply(false);
            if (it.bundle->detectionSource().isAutoDetected())
                aspects->makeReadOnly();
            connect(aspects, &BaseAspect::volatileValueChanged, this, [this, bundleId] {
                const int r = rowForBundleId(bundleId);
                if (r < 0)
                    return;
                setChanged(r, true);
                notifyRowChanged(r);
            });
        }
    }
    return aspects;
}

int ToolchainModel::rowForBundleId(const Id &id) const
{
    for (int row = 0; row < itemCount(); ++row) {
        const ToolchainTreeItem it = item(row);
        if (it.bundle && it.bundle->bundleId() == id)
            return row;
    }
    return -1;
}

void ToolchainModel::markRemoved(int row)
{
    if (isAdded(row)) {
        ToolchainTreeItem it = item(row);
        if (it.bundle) {
            delete m_configAspects.take(it.bundle->bundleId());
            it.bundle->deleteToolchains();
        }
    }
    GroupedModel::markRemoved(row);
}

void ToolchainModel::destroyBundle(int row)
{
    const ToolchainTreeItem it = item(row);
    if (it.bundle)
        delete m_configAspects.take(it.bundle->bundleId());
    removeItem(row);
}

QVariant ToolchainModel::variantData(int row, int column, int role) const
{
    if (role == Qt::FontRole)
        return {};
    if (role == Qt::DisplayRole && column == 0) {
        const ToolchainTreeItem it = item(row);
        if (it.bundle) {
            if (ToolchainConfigAspects *a = m_configAspects.value(it.bundle->bundleId()))
                return a->displayName().volatileValue();
        }
    }
    return toolchainBundleData(item(row).bundle, column, role);
}

void ToolchainModel::apply()
{
    // Write back the rows the user changed, which the auto-detected ones are not.
    for (int row = 0; row < itemCount(); ++row) {
        if (isRemoved(row))
            continue;
        const ToolchainTreeItem it = item(row);
        if (!it.bundle || it.bundle->detectionSource().isAutoDetected() || !isDirty(row))
            continue;
        if (ToolchainConfigAspects *a = m_configAspects.value(it.bundle->bundleId()))
            a->apply();
    }

    // Take the removed rows' aspects before deregistering: deregisterToolchains()
    // deletes the toolchains, after which bundleId() reads freed memory.
    QList<ToolchainConfigAspects *> aspectsToDelete;
    for (int row = 0; row < itemCount(); ++row) {
        if (isRemoved(row)) {
            const ToolchainTreeItem it = item(row);
            if (it.bundle)
                aspectsToDelete << m_configAspects.take(it.bundle->bundleId());
        }
    }

    // Deregister removed items.
    {
        GuardLocker locker(m_deregisterGuard);
        for (int row = 0; row < itemCount(); ++row) {
            if (!isRemoved(row))
                continue;
            const ToolchainTreeItem it = item(row);
            if (it.bundle)
                ToolchainManager::deregisterToolchains(it.bundle->toolchains());
        }
    }

    // Register added toolchains. The manager takes ownership of registered pointers.
    // Rejected duplicates are removed from the model bundle so they can be freed safely.
    QStringList removedTcs;
    Toolchains notRegisteredTcs;
    {
        GuardLocker locker(m_registerGuard);
        for (int row = 0; row < itemCount(); ++row) {
            if (!isAdded(row))
                continue;
            ToolchainTreeItem it = item(row);
            if (!it.bundle)
                continue;
            const Toolchains notRegistered = ToolchainManager::registerToolchains(it.bundle->toolchains());
            if (!notRegistered.isEmpty()) {
                removedTcs << Utils::transform(notRegistered, &Toolchain::displayName);
                for (Toolchain *tc : notRegistered)
                    it.bundle->removeToolchain(tc);
                if (it.bundle->toolchains().isEmpty()) {
                    // All toolchains were rejected. markRemoved() frees them via deleteToolchains()
                    // on the stored item's copy, so do NOT add them to notRegisteredTcs.
                    // Must be done inside the guard so that signal handlers cannot re-insert a bundle
                    // at this row before GroupedModel::apply() runs.
                    markRemoved(row--);
                } else {
                    notRegisteredTcs << notRegistered;
                    setVolatileItem(row, it);
                }
            }
        }
    }
    qDeleteAll(notRegisteredTcs);

    // GroupedModel::apply() commits all non-removed volatile items (including the
    // now-registered added rows), so no explicit removal of added rows is needed.

    qDeleteAll(aspectsToDelete);

    GroupedModel::apply();

    // Show duplicate toolchain dialog.
    if (removedTcs.count() == 1) {
        QMessageBox::warning(Core::ICore::dialogParent(),
                             Tr::tr("Duplicate Compilers Detected"),
                             Tr::tr("The following compiler was already configured:<br>"
                                    "&nbsp;%1<br>"
                                    "It was not configured again.")
                                 .arg(removedTcs.at(0)));
    } else if (!removedTcs.isEmpty()) {
        QMessageBox::warning(Core::ICore::dialogParent(),
                             Tr::tr("Duplicate Compilers Detected"),
                             Tr::tr("The following compilers were already configured:<br>"
                                    "&nbsp;%1<br>"
                                    "They were not configured again.")
                                 .arg(removedTcs.join(QLatin1String(",<br>&nbsp;"))));
    }
}

class ToolChainOptionsWidget final : public AspectContainer
{
public:
    ToolChainOptionsWidget();

    void toolChainSelectionChanged();
    void createToolchains(ToolchainFactory *factory, const QList<Id> &languages);

    void handleToolchainsRegistered(const Toolchains &toolchains);
    void handleToolchainsDeregistered(const Toolchains &toolchains);

    void redetectToolchains();

    void apply() override;
    void cancel() override;
    bool isDirty() const override
    {
        return m_model.isDirty()
               || m_detectionSettings != ToolchainManager::detectionSettings();
    }

private:
    void removeAll();
    void editDetectionSettings();

    ToolchainModel m_model;

    DeviceSelectionAspect m_device{this};
    GroupedListAspect m_toolchains{this};
    ActionAspect m_addButton{this};
    ActionAspect m_removeAllButton{this};
    ActionAspect m_redetectButton{this};
    ActionAspect m_detectionSettingsButton{this};
    // What the kind of toolchain being looked at asks for, which changes with
    // the row. Not owned: the model keeps one per bundle.
    ContainerAspect m_configuration{this};

    ToolchainDetectionSettings m_detectionSettings;
};

ToolChainOptionsWidget::ToolChainOptionsWidget()
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/ToolchainsPage.qml"));

    m_detectionSettings = ToolchainManager::detectionSettings();

    m_device.setQmlName("Device");
    m_device.setLabelText(Tr::tr("Device:"));
    m_device.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

    m_toolchains.setQmlName("Toolchains");
    m_toolchains.setModel(&m_model);
    // An SDK-provided toolchain is not the user's to take away, and one that
    // is not a toolchain at all is not worth copying.
    m_toolchains.setCanRemoveRow([this](int row) {
        const ToolchainTreeItem it = m_model.item(row);
        return it.bundle && !it.bundle->detectionSource().isSdkProvided();
    });
    m_toolchains.setCanCloneRow([this](int row) {
        const ToolchainTreeItem it = m_model.item(row);
        return it.bundle && it.bundle->validity() != ToolchainBundle::Valid::None;
    });

    // Adding a toolchain is picking a kind, so the button offers rather than
    // does. The kinds are fixed once the plugins are loaded.
    m_addButton.setQmlName("Add");
    m_addButton.setActionText(Tr::tr("Add"));
    QList<AspectPresentation::Choice> kinds;
    for (ToolchainFactory *factory : ToolchainFactory::allToolchainFactories()) {
        if (!factory->canCreate() || factory->supportedLanguages().isEmpty())
            continue;
        kinds.append({.display = factory->displayName(),
                      .id = factory->supportedToolchainType().toSetting()});
    }
    m_addButton.setChoices(kinds);
    m_addButton.setOnChoice([this](const QVariant &id) {
        const Id type = Id::fromSetting(id);
        for (ToolchainFactory *factory : ToolchainFactory::allToolchainFactories()) {
            if (factory->supportedToolchainType() == type) {
                createToolchains(factory, factory->supportedLanguages());
                return;
            }
        }
    });

    m_removeAllButton.setQmlName("RemoveAll");
    m_removeAllButton.setActionText(Tr::tr("Remove All"));
    m_removeAllButton.setAction([this] { removeAll(); });

    m_redetectButton.setQmlName("Redetect");
    m_redetectButton.setActionText(Tr::tr("Re-detect"));
    m_redetectButton.setAction([this] { redetectToolchains(); });

    m_detectionSettingsButton.setQmlName("DetectionSettings");
    m_detectionSettingsButton.setActionText(Tr::tr("Auto-detection Settings..."));
    m_detectionSettingsButton.setAction([this] { editDetectionSettings(); });

    m_configuration.setQmlName("Configuration");

    const QList<ToolchainBundle> bundles = ToolchainBundle::collectBundles(
        ToolchainBundle::HandleMissing::CreateAndRegister);
    for (const ToolchainBundle &b : bundles)
        m_model.insertBundle(b);

    // Behaviour, not layout.
    connect(ToolchainManager::instance(), &ToolchainManager::toolchainsRegistered,
            this, &ToolChainOptionsWidget::handleToolchainsRegistered);
    connect(ToolchainManager::instance(), &ToolchainManager::toolchainsDeregistered,
            this, &ToolChainOptionsWidget::handleToolchainsDeregistered);
    connect(&m_toolchains, &GroupedListAspect::currentRowChanged,
            this, &ToolChainOptionsWidget::toolChainSelectionChanged);
    connect(ToolchainManager::instance(), &ToolchainManager::toolchainsChanged,
            this, &ToolChainOptionsWidget::toolChainSelectionChanged);

    connect(&m_device, &DeviceSelectionAspect::currentDeviceChanged, this, [this] {
        const IDeviceConstPtr dev = m_device.currentDevice();
        const FilePath deviceRoot = dev ? dev->rootPath() : FilePath();
        m_model.setExtraFilter(deviceRoot.isEmpty()
            ? GroupedModel::Filter{}
            : GroupedModel::Filter{[this, deviceRoot](int row) {
                  const ToolchainTreeItem it = m_model.item(row);
                  if (!it.bundle)
                      return true;
                  const FilePath path = it.bundle->get(&Toolchain::compilerCommand);
                  return path.isEmpty() || path.isSameDevice(deviceRoot);
              }});
    });

    toolChainSelectionChanged();
}

void ToolChainOptionsWidget::removeAll()
{
    bool anyRemoved = false;
    for (int row = m_model.itemCount() - 1; row >= 0; --row) {
        if (!m_model.mapFromSource(m_model.index(row, 0)).isValid())
            continue;
        const ToolchainTreeItem it = m_model.item(row);
        if (it.bundle && !it.bundle->detectionSource().isSdkProvided()) {
            m_model.markRemoved(row);
            anyRemoved = true;
        }
    }
    if (anyRemoved)
        Utils::checkSettingsDirty();
}

void ToolChainOptionsWidget::editDetectionSettings()
{
    DetectionSettingsDialog dlg(m_detectionSettings, Core::ICore::dialogParent());
    if (dlg.exec() != QDialog::Accepted)
        return;
    const bool old = m_detectionSettings.detectX64AsX32;
    m_detectionSettings = dlg.settings();
    if (m_detectionSettings.detectX64AsX32 != old)
        Utils::checkSettingsDirty();
}

void ToolChainOptionsWidget::handleToolchainsRegistered(const Toolchains &toolchains)
{
    if (m_model.m_registerGuard.isLocked())
        return;
    GuardLocker locker(m_model.m_registerGuard);

    // Check if bundle is already in the model (e.g. one of our pending adds).
    if (!toolchains.isEmpty() && m_model.rowForBundleId(toolchains.first()->bundleId()) >= 0)
        return;

    // External registration: add new bundles.
    const QList<ToolchainBundle> bundles = ToolchainBundle::collectBundles(
        toolchains, ToolchainBundle::HandleMissing::CreateAndRegister);
    for (const ToolchainBundle &bundle : bundles)
        m_model.insertBundle(bundle);
}

void ToolChainOptionsWidget::handleToolchainsDeregistered(const Toolchains &toolchains)
{
    if (m_model.m_deregisterGuard.isLocked())
        return;
    GuardLocker locker(m_model.m_deregisterGuard);

    // Find affected rows (one per bundle).
    QList<int> affectedRows;
    for (Toolchain * const tc : toolchains) {
        const int row = m_model.rowForBundleId(tc->bundleId());
        if (row >= 0 && !affectedRows.contains(row))
            affectedRows << row;
    }

    // Process in reverse order to preserve row indices during removal.
    std::sort(affectedRows.begin(), affectedRows.end(), std::greater<>());
    for (const int row : std::as_const(affectedRows)) {
        const ToolchainTreeItem it = m_model.item(row);
        if (it.bundle) {
            // Deregister remaining toolchains in the bundle (guard prevents re-entry).
            const Toolchains remaining = Utils::filtered(
                it.bundle->toolchains(), [&toolchains](Toolchain *tc) {
                    return !toolchains.contains(tc);
                });
            ToolchainManager::deregisterToolchains(remaining);
        }
        m_model.destroyBundle(row);
    }

}

void ToolChainOptionsWidget::redetectToolchains()
{
    // The second element is the set of toolchains for the respective bundle that were re-discovered.
    using ItemToCheck = std::pair<int, Toolchains>;
    QList<ItemToCheck> itemsToRemove;

    Toolchains knownTcs;

    // Step 1: All previously system-detected items are candidates for removal.
    for (int row = 0; row < m_model.itemCount(); ++row) {
        if (!m_model.mapFromSource(m_model.index(row, 0)).isValid())
            continue;
        const ToolchainTreeItem it = m_model.item(row);
        if (!it.bundle)
            continue;
        if (it.bundle->detectionSource().isSystemDetected())
            itemsToRemove << std::make_pair(row, Toolchains());
        else
            knownTcs << it.bundle->toolchains();
    }

    Toolchains toAdd;
    ToolchainManager::resetBadToolchains();

    // Step 2: Re-detect toolchains.
    for (const IDeviceConstPtr &device : m_device.selectedDevices()) {
        const DetectionSource detectionSource = device->id() == Constants::DESKTOP_DEVICE_ID
                                                    ? DetectionSource::FromSystem
                                                    : DetectionSource::Manual;
        for (ToolchainFactory *f : ToolchainFactory::allToolchainFactories()) {
            const ToolchainDetector detector(knownTcs, device, device->toolSearchPaths());
            for (Toolchain * const tc : f->autoDetect(detector)) {
                if (knownTcs.contains(tc))
                    continue;
                tc->setDetectionSource(detectionSource);
                knownTcs << tc;
                const auto matchItem = [&](const ItemToCheck &item) {
                    const ToolchainTreeItem it = m_model.item(item.first);
                    return it.bundle && Utils::contains(it.bundle->toolchains(),
                                                        [&](Toolchain *btc) {
                                                            return *btc == *tc;
                                                        });
                };
                if (const auto item
                    = std::find_if(itemsToRemove.begin(), itemsToRemove.end(), matchItem);
                    item != itemsToRemove.end()) {
                    item->second << tc;
                    continue;
                }
                toAdd << tc;
            }
        }
    }

    // Step 3: Items whose toolchains were all re-discovered are no longer candidates for removal.
    //    Instead, delete the re-discovered toolchains.
    //    Conversely, if not all toolchains of the bundle were re-discovered, we remove the existing
    //    item and the newly discovered toolchains are marked for re-bundling.
    for (const auto &[row, newToolchains] : std::as_const(itemsToRemove)) {
        const ToolchainTreeItem it = m_model.item(row);
        if (it.bundle && it.bundle->toolchains().size() == newToolchains.size()) {
            qDeleteAll(newToolchains);
        } else {
            toAdd << newToolchains;
            m_model.markRemoved(row);
        }
    }

    // Step 4: Create new bundles and add items for them.
    const QList<ToolchainBundle> newBundles
        = ToolchainBundle::collectBundles(toAdd, ToolchainBundle::HandleMissing::CreateOnly);
    for (const ToolchainBundle &bundle : newBundles)
        m_model.addBundle(bundle);

    if (!itemsToRemove.isEmpty() || !toAdd.isEmpty())
        Utils::checkSettingsDirty();
}

void ToolChainOptionsWidget::toolChainSelectionChanged()
{
    const int row = m_toolchains.currentRow();
    ToolchainConfigAspects *aspects = nullptr;
    if (row >= 0 && !m_model.isRemoved(row))
        aspects = m_model.configAspects(row);
    if (aspects) {
        if (const IDeviceConstPtr dev = m_device.currentDevice())
            aspects->setFallbackBrowsePath(dev->rootPath());
    }
    m_configuration.setContainer(aspects);
}

void ToolChainOptionsWidget::apply()
{
    m_model.apply();
    ToolchainManager::setDetectionSettings(m_detectionSettings);
}

void ToolChainOptionsWidget::cancel()
{
    m_model.cancel();
    m_detectionSettings = ToolchainManager::detectionSettings();
}

void ToolChainOptionsWidget::createToolchains(ToolchainFactory *factory, const QList<Id> &languages)
{
    QTC_ASSERT(factory, return);
    QTC_ASSERT(factory->canCreate(), return);

    const Id bundleId = Id::generate();
    Toolchains toolchains;
    for (const Id lang : languages) {
        Toolchain *tc = factory->create();
        QTC_ASSERT(tc, return);

        tc->setDetectionSource(DetectionSource::Manual);
        tc->setLanguage(lang);
        tc->setBundleId(bundleId);
        toolchains << tc;
    }

    const ToolchainBundle bundle(toolchains, ToolchainBundle::HandleMissing::CreateOnly);
    m_toolchains.setCurrentRow(m_model.addBundle(bundle));
}

#ifdef WITH_TESTS
class ToolchainOptionsPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheCurrentRowIsWhatThePageOffersToConfigure()
    {
        // The page draws whatever the current toolchain asks for and knows
        // about no kind in particular, so what it hands to the form has to be
        // that row's own aspects and has to follow the selection.
        ToolChainOptionsWidget page;
        auto configuration = page.aspect<ContainerAspect>();
        QVERIFY(configuration);

        auto list = page.aspect<GroupedListAspect>();
        QVERIFY(list);
        auto model = static_cast<ToolchainModel *>(list->model());
        QVERIFY(model);
        if (model->itemCount() < 2)
            QSKIP("Fewer than two toolchains are configured here");

        // Two rows that are actually shown: a filtered-out one cannot be made
        // current, so asking for it would prove nothing.
        QList<int> rows;
        for (int row = 0; row < model->itemCount() && rows.size() < 2; ++row) {
            if (model->mapFromSource(model->index(row, 0)).isValid())
                rows << row;
        }
        if (rows.size() < 2)
            QSKIP("Fewer than two toolchains are shown here");

        list->setCurrentRow(rows.first());
        AspectContainer * const first = configuration->container();
        QVERIFY(first);
        QCOMPARE(first, model->configAspects(rows.first()));

        list->setCurrentRow(rows.last());
        AspectContainer * const second = configuration->container();
        QVERIFY(second);
        QCOMPARE(second, model->configAspects(rows.last()));
        QVERIFY(second != first);

        // And going back gets the same aspects, not a fresh set: what was
        // typed into a row is still there when the user returns to it.
        list->setCurrentRow(rows.first());
        QCOMPARE(configuration->container(), first);
    }

    void testTypingIntoAToolchainMarksItsRowChanged()
    {
        // Renaming a toolchain has to reach the row that names it and make
        // Apply do something. The page sees that through the aspects rather
        // than through a signal a widget used to emit.
        ToolChainOptionsWidget page;
        auto list = page.aspect<GroupedListAspect>();
        QVERIFY(list);
        auto model = static_cast<ToolchainModel *>(list->model());
        QVERIFY(model);

        int row = -1;
        for (int r = 0; r < model->itemCount(); ++r) {
            if (!model->mapFromSource(model->index(r, 0)).isValid())
                continue;
            if (ToolchainConfigAspects * const aspects = model->configAspects(r)) {
                if (aspects->displayName().isEnabled()) {
                    row = r;
                    break;
                }
            }
        }
        if (row < 0)
            QSKIP("No toolchain here can be renamed");

        ToolchainConfigAspects * const aspects = model->configAspects(row);
        QVERIFY(!model->isDirty());
        aspects->displayName().setValue(aspects->displayName().volatileValue() + " (edited)");
        QVERIFY(model->isDirty());
        // And the list shows what is being typed, not what was saved.
        QCOMPARE(model->index(row, 0).data(Qt::DisplayRole).toString(),
                 aspects->displayName().volatileValue());
    }
};

QObject *createToolchainOptionsPageTest()
{
    return new ToolchainOptionsPageTest;
}
#endif // WITH_TESTS

// ToolChainOptionsPage

ToolChainOptionsPage::ToolChainOptionsPage()
{
    setId(Constants::TOOLCHAIN_SETTINGS_PAGE_ID);
    setDisplayName(Tr::tr("Compilers"));
    setCategory(Constants::KITS_SETTINGS_CATEGORY);
    setSettingsProvider([] {
        static GuardedObject<ToolChainOptionsWidget> theAspects;
        return theAspects.get();
    });
}

} // namespace ProjectExplorer::Internal


#ifdef WITH_TESTS
#include "toolchainoptionspage.moc"
#endif

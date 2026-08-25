// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "kitoptionspage.h"

#include "devicesupport/devicekitaspects.h"
#include "devicesupport/idevicefactory.h"
#include "filterkitaspectsdialog.h"
#include "kit.h"
#include "kitaspect.h"
#include "kitdata.h"
#include "kitmanager.h"
#include "projectexplorerconstants.h"
#include "projectexplorertr.h"
#include "task.h"

#include <coreplugin/icore.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/filedialogs.h>
#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/groupedlistaspect.h>
#include <utils/groupedmodel.h>
#include <utils/groupedview.h>
#include <utils/guiutils.h>
#include <utils/id.h>
#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/shutdownguard.h>
#include <utils/stringutils.h>
#include <utils/utilsicons.h>
#include <utils/variablechooser.h>

#include <QAction>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSet>
#include <QSizePolicy>
#include <QToolButton>

const char WORKING_COPY_KIT_ID[] = "modified kit";

using namespace Core;
using namespace Utils;

namespace ProjectExplorer::Internal {

// KitModel

class KitModel final : public TypedGroupedModel<KitData>
{
    Q_OBJECT

public:
    explicit KitModel();

    int rowForId(Id kitId) const;
    int rowForOriginalKit(Kit *k) const;
    Kit *kitForRow(int row) const { return KitManager::kit(item(row).m_id); }

    void apply() final;

    int cloneRow(int row) final;
    void markRemoved(int row) final;

    int markForAddition(Kit *baseKit);

    bool isNameUnique(int row) const;

    Kit *modifiedKit() { return &m_modifiedKit; }
    void commitModifiedKit(int row);

signals:
    void kitStateChanged();

private:
    QVariant variantData(int row, int column, int role) const final;
    void addKit(Kit *k);
    void updateKit(Kit *k);
    void removeKit(Kit *k);
    void changeDefaultKit();

    bool m_isRegistering = false;
    Kit m_modifiedKit{Id(WORKING_COPY_KIT_ID)};
};

KitModel::KitModel()
{
    setShowDefault(true);
    setHeader({Tr::tr("Name")});
    setFilters(Constants::msgAutoDetected(), {{Tr::tr("Manual"), [this](int row) {
        return !item(row).m_detectionSource.isAutoDetected();
    }}});

    if (KitManager::isLoaded()) {
        for (Kit *k : KitManager::sortedKits())
            addKit(k);
        changeDefaultKit();
    }
    setDefaultRow(defaultRow());

    connect(KitManager::instance(), &KitManager::kitAdded,
            this, &KitModel::addKit);
    connect(KitManager::instance(), &KitManager::kitUpdated,
            this, &KitModel::updateKit);
    connect(KitManager::instance(), &KitManager::unmanagedKitUpdated,
            this, &KitModel::updateKit);
    connect(KitManager::instance(), &KitManager::kitRemoved,
            this, &KitModel::removeKit);
    connect(KitManager::instance(), &KitManager::defaultkitChanged,
            this, &KitModel::changeDefaultKit);
}

static QString displayNameOf(Kit *kit, const KitData &d)
{
    const QString name = d.unexpandedDisplayName();
    if (kit)
        return kit->macroExpander()->expand(name);
    Kit tempKit{Id(WORKING_COPY_KIT_ID)};
    tempKit.copyFrom(d);
    return tempKit.macroExpander()->expand(name);
}

QVariant KitModel::variantData(int row, int /*column*/, int role) const
{
    const KitData d = item(row);
    Kit *kit = KitManager::kit(d.m_id);
    switch (role) {
    case Qt::DisplayRole:
        return displayNameOf(kit, d);
    case Qt::DecorationRole:
        if (kit) {
            if (!kit->isValid())
                return Icons::CRITICAL.icon();
            if (kit->hasWarning() || !isNameUnique(row))
                return Icons::WARNING.icon();
            return kit->icon();
        } else if (!isNameUnique(row)) {
            return Icons::WARNING.icon();
        }
        return d.icon();
    case Qt::ToolTipRole: {
        Tasks tmp;
        if (!isNameUnique(row))
            tmp.append(CompileTask(Task::Warning, Tr::tr("Display name is not unique.")));
        Kit tempKit{Id(WORKING_COPY_KIT_ID)};
        tempKit.copyFrom(d);
        return tempKit.toHtml(tmp);
    }
    default:
        return {};
    }
}

int KitModel::rowForOriginalKit(Kit *k) const
{
    for (int row = 0; row < itemCount(); ++row) {
        if (item(row).m_id == k->id())
            return row;
    }
    return -1;
}

int KitModel::rowForId(Id kitId) const
{
    for (int row = 0; row < itemCount(); ++row) {
        if (item(row).m_id == kitId)
            return row;
    }
    return -1;
}

void KitModel::commitModifiedKit(int row)
{
    QTC_ASSERT(row >= 0 && row < itemCount(), return);
    KitData d = m_modifiedKit.kitData();
    d.m_id = item(row).m_id;
    setVolatileItem(row, d);
}

void KitModel::apply()
{
    // Collect kits to deregister (removed rows)
    QList<Kit *> kitsToDeregister;
    for (int row = 0; row < itemCount(); ++row) {
        if (isRemoved(row)) {
            if (Kit *kit = KitManager::kit(item(row).m_id))
                kitsToDeregister.append(kit);
        }
    }

    // Apply non-removed dirty/added rows
    for (int row = 0; row < itemCount(); ++row) {
        if (isRemoved(row))
            continue;
        if (!isAdded(row) && !isDirty(row))
            continue;
        const KitData d = item(row);
        if (Kit *kit = KitManager::kit(d.m_id)) {
            kit->copyFrom(d);
            KitManager::notifyAboutUpdate(kit);
        } else {
            m_isRegistering = true;
            KitManager::registerKit([&](Kit *k) { k->copyFrom(d); });
            m_isRegistering = false;
        }
    }

    // Apply default kit selection
    if (const int defRow = defaultRow(); defRow >= 0) {
        if (Kit *kit = KitManager::kit(item(defRow).m_id))
            KitManager::setDefaultKit(kit);
    }

    GroupedModel::apply();

    for (Kit *k : kitsToDeregister)
        KitManager::deregisterKit(k);
}

int KitModel::cloneRow(int row)
{
    Q_UNUSED(row)
    return markForAddition(modifiedKit());
}

void KitModel::markRemoved(int row)
{
    const bool wasRemoved = isRemoved(row);
    GroupedModel::markRemoved(row);
    if (wasRemoved && isOriginalDefault(row))
        setVolatileDefaultRow(row);
    notifyAllRowsChanged();
    emit kitStateChanged();
}

int KitModel::markForAddition(Kit *baseKit)
{
    QStringList allNames;
    for (int row = 0; row < itemCount(); ++row)
        allNames << item(row).unexpandedDisplayName();
    const QString baseName = baseKit
        ? Tr::tr("Clone of %1").arg(baseKit->unexpandedDisplayName())
        : Tr::tr("Unnamed");
    const QString newName = Utils::makeUniquelyNumbered(baseName, allNames);

    Kit tempKit{Id(WORKING_COPY_KIT_ID)};
    if (baseKit)
        tempKit.copyFrom(baseKit);
    else
        tempKit.setup();
    tempKit.setUnexpandedDisplayName(newName);

    KitData kd = tempKit.kitData();
    kd.m_detectionSource = DetectionSource::Manual;
    kd.m_id = {};
    const int newRow = itemCount();
    appendVolatileItem(kd);

    if (defaultRow() < 0 || isRemoved(defaultRow()))
        setVolatileDefaultRow(newRow);

    notifyAllRowsChanged();
    return newRow;
}

void KitModel::addKit(Kit *k)
{
    if (m_isRegistering) {
        for (int row = 0; row < itemCount(); ++row) {
            if (isAdded(row) && !item(row).m_id.isValid()) {
                KitData d = item(row);
                d.m_id = k->id();
                setVolatileItem(row, d);
                notifyRowChanged(row);
                return;
            }
        }
        return;
    }

    appendVariant(toVariant(k->kitData()));

    if (k == KitManager::defaultKit())
        setDefaultRow(rowForId(k->id()));

    notifyAllRowsChanged();
    emit kitStateChanged();
}

void KitModel::updateKit(Kit *k)
{
    const int row = rowForOriginalKit(k);
    if (row < 0)
        return;

    if (!isDirty(row)) {
        // External update with no local edits: refresh committed and volatile variant
        resetItem(row, k->kitData());
    }

    notifyAllRowsChanged();
    emit kitStateChanged();
}

void KitModel::removeKit(Kit *k)
{
    const int row = rowForOriginalKit(k);
    if (row < 0)
        return;
    if (isRemoved(row))
        return;  // already pending deregistration via apply()
    removeItem(row);
    notifyAllRowsChanged();
    emit kitStateChanged();
}

void KitModel::changeDefaultKit()
{
    setVolatileDefaultRow(rowForOriginalKit(KitManager::defaultKit()));
}

bool KitModel::isNameUnique(int row) const
{
    const QString name = displayNameOf(kitForRow(row), item(row));
    for (int r = 0; r < itemCount(); ++r) {
        if (r != row && !isRemoved(r) && displayNameOf(kitForRow(r), item(r)) == name)
            return false;
    }
    return true;
}

// KitOptionsPageWidget

class KitOptionsPageWidget : public AspectContainer
{
public:
    KitOptionsPageWidget();
    ~KitOptionsPageWidget() override;

    void kitSelectionChanged(int newRow);
    void addNewKit();
    void updateState();
    void scrollToSelectedKit();

    void apply() override;
    bool isDirty() const override { return m_model.isDirty(); }

private:
    void onDirty();
    void setFocusToName();
    void load(const KitData &workingCopySrc, int row = -1);

    void updateVisibility();
    void collectKitAspects();
    void chooseIcon(const QVariant &choice);
    void refreshIconChoices();
    void setDisplayName();
    void setFileSystemFriendlyName();
    void workingCopyWasUpdated(Kit *k);

    KitModel m_model;

    GroupedListAspect m_kits{this};
    ActionAspect m_addButton{this};
    ActionAspect m_filterButton{this};
    ActionAspect m_defaultFilterButton{this};

    // Shown for the kit that is current, hidden when none is.
    AspectContainer m_details{this};
    // The name and the icon read as one thing, so they are one row.
    AspectContainer m_nameRow{&m_details};
    StringAspect m_nameEdit{&m_nameRow};
    ActionAspect m_iconButton{&m_nameRow};
    StringAspect m_fileSystemFriendlyNameLineEdit{&m_details};
    // The eighteen kit aspect rows, which are the same objects whichever kit
    // is current: selecting one reloads them rather than making new ones.
    AspectContainer m_kitAspectRows;
    ContainerAspect m_kitAspectsShown{&m_details};

    QList<KitAspect *> m_kitAspects;
    bool m_fixingKit = false;
    bool m_loading = false;
};

KitOptionsPageWidget::KitOptionsPageWidget()
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/KitsPage.qml"));

    m_kits.setQmlName("Kits");
    m_kits.setModel(&m_model);
    m_kits.setShowsDefault(true);
    // An SDK-provided kit is not the user's to take away.
    m_kits.setCanRemoveRow(
        [this](int row) { return !m_model.item(row).m_detectionSource.isSdkProvided(); });

    m_addButton.setQmlName("Add");
    m_addButton.setActionText(Tr::tr("Add"));
    m_addButton.setAction([this] { addNewKit(); });

    m_filterButton.setQmlName("Filter");
    m_filterButton.setActionText(Tr::tr("Settings Filter..."));
    m_filterButton.setToolTip(Tr::tr("Choose which settings to display for this kit."));
    m_filterButton.setAction([this] {
        QTC_ASSERT(m_kits.currentRow() >= 0, return);
        FilterKitAspectsDialog dlg(m_model.modifiedKit(), Core::ICore::dialogParent());
        if (dlg.exec() == QDialog::Accepted) {
            m_model.modifiedKit()->setIrrelevantAspects(dlg.irrelevantAspects());
            updateVisibility();
        }
    });

    m_defaultFilterButton.setQmlName("DefaultFilter");
    m_defaultFilterButton.setActionText(Tr::tr("Default Settings Filter..."));
    m_defaultFilterButton.setToolTip(
        Tr::tr("Choose which settings to display for all kits by default."));
    m_defaultFilterButton.setAction([this] {
        FilterKitAspectsDialog dlg(nullptr, Core::ICore::dialogParent());
        if (dlg.exec() == QDialog::Accepted) {
            KitManager::setIrrelevantAspects(dlg.irrelevantAspects());
            updateVisibility();
        }
    });

    m_details.setQmlName("Details");

    m_nameRow.setQmlName("NameRow");
    m_nameRow.setInlineRow(true);
    m_nameRow.setLabelText(Tr::tr("Name:"));
    m_nameRow.setToolTip(Tr::tr("Kit name and icon."));

    m_nameEdit.setQmlName("Name");
    m_nameEdit.setDisplayStyle(StringAspect::LineEditDisplay);

    m_iconButton.setQmlName("Icon");
    m_iconButton.setToolTip(Tr::tr("Kit icon."));
    m_iconButton.setOnChoice([this](const QVariant &choice) { chooseIcon(choice); });

    const QString fsToolTip =
        "<p>" + Tr::tr("The name of the kit suitable for generating "
               "directory names. This value is used for the variable %1, "
               "which for example determines the name of the shadow build directory.")
               .arg("<i>Kit:FileSystemName</i>") + "</p>";
    m_fileSystemFriendlyNameLineEdit.setQmlName("FileSystemName");
    m_fileSystemFriendlyNameLineEdit.setLabelText(Tr::tr("File system name:"));
    m_fileSystemFriendlyNameLineEdit.setDisplayStyle(StringAspect::LineEditDisplay);
    m_fileSystemFriendlyNameLineEdit.setToolTip(fsToolTip);
    m_fileSystemFriendlyNameLineEdit.setValidationFunction([](const QString &text) -> Result<> {
        static const QRegularExpression allowed("^[A-Za-z0-9_-]*$");
        if (allowed.match(text).hasMatch())
            return ResultOk;
        return ResultError(Tr::tr("Only letters, digits, dashes and underscores."));
    });

    m_kitAspectsShown.setQmlName("KitAspects");
    collectKitAspects();
    m_kitAspectsShown.setContainer(&m_kitAspectRows);
    // The button stands for the icon, so it has one before a kit is current.
    refreshIconChoices();

    // Behaviour, not layout.
    connect(&m_model, &Internal::KitModel::kitStateChanged,
            this, &KitOptionsPageWidget::updateState);
    connect(KitManager::instance(), &KitManager::unmanagedKitUpdated,
            this, &KitOptionsPageWidget::workingCopyWasUpdated);
    connect(&m_kits, &GroupedListAspect::currentRowChanged,
            this, [this](int, int newRow) { kitSelectionChanged(newRow); });
    connect(KitManager::instance(), &KitManager::kitAdded,
            this, &KitOptionsPageWidget::updateState);
    connect(KitManager::instance(), &KitManager::kitRemoved,
            this, &KitOptionsPageWidget::updateState);
    connect(KitManager::instance(), &KitManager::kitUpdated, this, [this](Kit *k) {
        const int row = m_model.rowForOriginalKit(k);
        const int currentRow = m_kits.currentRow();
        if (row == currentRow && currentRow >= 0)
            load(m_model.item(currentRow), currentRow);
        updateState();
    });
    connect(&m_kits, &GroupedListAspect::currentCloned,
            this, &KitOptionsPageWidget::setFocusToName);

    m_nameEdit.addOnVolatileValueChanged(this, [this] { setDisplayName(); });
    m_fileSystemFriendlyNameLineEdit.addOnVolatileValueChanged(
        this, [this] { setFileSystemFriendlyName(); });

    // Another page may have changed what a kit can be pointed at while this
    // one was not on screen, and building the page is not showing it.
    connect(this, &AspectContainer::shown, this, [this] {
        if (!m_details.isVisible())
            return;
        for (KitAspect *aspect : std::as_const(m_kitAspects))
            aspect->refresh();
    });

    // The name may name the kit's own variables, so it expands against
    // whichever kit is being looked at.
    m_nameEdit.setMacroExpander(m_model.modifiedKit()->macroExpander());

    scrollToSelectedKit();
    // Nothing is current until something is picked, and then the details are
    // about that kit rather than about the working copy they start on.
    kitSelectionChanged(m_kits.currentRow());
    updateState();
}

KitOptionsPageWidget::~KitOptionsPageWidget()
{
    qDeleteAll(m_kitAspects);
    m_kitAspects.clear();

    // Make sure our working copy did not get registered somehow:
    QTC_CHECK(!contains(KitManager::kits(), equal(&Kit::id, Id(WORKING_COPY_KIT_ID))));
}

void KitOptionsPageWidget::scrollToSelectedKit()
{
    const int row = m_model.rowForId(
        Core::preselectedOptionsPageItem(Constants::KITS_SETTINGS_PAGE_ID));
    m_kits.setCurrentRow(row);
}

void KitOptionsPageWidget::apply()
{
    m_model.apply();
    updateState();
}

void KitOptionsPageWidget::kitSelectionChanged(int newRow)
{
    if (newRow >= 0) {
        load(m_model.item(newRow), newRow);
        m_details.setVisible(true);
    } else {
        m_details.setVisible(false);
    }

    updateState();
}

void KitOptionsPageWidget::addNewKit()
{
    const int row = m_model.markForAddition(nullptr);
    m_kits.setCurrentRow(row);

    if (m_kits.currentRow() >= 0)
        setFocusToName();
}


void KitOptionsPageWidget::updateState()
{
    const int row = m_kits.currentRow();
    const bool hasRow = row >= 0;
    const bool isRemoved = hasRow && m_model.isRemoved(row);
    m_filterButton.setEnabled(hasRow && !isRemoved);
}

void KitOptionsPageWidget::onDirty()
{
    const int row = m_kits.currentRow();
    if (row < 0)
        return;
    m_loading = true;
    for (KitAspect *aspect : std::as_const(m_kitAspects))
        aspect->apply();
    m_loading = false;
    m_model.commitModifiedKit(row);
    m_model.notifyAllRowsChanged();
}

void KitOptionsPageWidget::setFocusToName()
{
    m_nameEdit.setFocusToInputField();
}

void KitOptionsPageWidget::load(const KitData &workingCopySrc, int row)
{
    m_loading = true;

    m_model.modifiedKit()->copyFrom(workingCopySrc);
    for (KitAspect *aspect : std::as_const(m_kitAspects))
        aspect->reload();

    refreshIconChoices();
    m_nameEdit.setValue(m_model.modifiedKit()->unexpandedDisplayName());
    m_fileSystemFriendlyNameLineEdit.setValue(
        m_model.modifiedKit()->customFileSystemFriendlyName());

    m_loading = false;

    // KitAspect::refresh() may normalize invalid stored values as a side effect of reload().
    // If the row had no prior user changes, update the committed baseline so the
    // normalization doesn't register as a user edit.
    if (row >= 0 && !m_model.isDirty(row)) {
        KitData normalizedData = m_model.modifiedKit()->kitData();
        normalizedData.m_id = workingCopySrc.m_id;
        if (normalizedData != workingCopySrc)
            m_model.resetItem(row, normalizedData);
    }

    updateVisibility();

    if (m_model.modifiedKit()->detectionSource().isAutoDetected()) {
        for (KitAspect *aspect : std::as_const(m_kitAspects))
            aspect->makeStickySubWidgetsReadOnly();
    }
}


void KitOptionsPageWidget::collectKitAspects()
{
    QHash<Id, KitAspect *> aspectsById;
    for (KitAspectFactory *factory : KitManager::kitAspectFactories()) {
        QTC_ASSERT(factory, continue);

        KitAspect *aspect = factory->createKitAspect(m_model.modifiedKit());
        QTC_ASSERT(aspect, continue);
        QTC_ASSERT(!m_kitAspects.contains(aspect), continue);

        m_kitAspects.append(aspect);
        aspectsById.insert(factory->id(), aspect);

        connect(aspect->mutableAction(), &QAction::toggled,
            this, [this] { if (!m_loading) onDirty(); });
        connect(aspect, &BaseAspect::volatileValueChanged,
            this, [this] { if (!m_loading) onDirty(); });
    }

    QSet<KitAspect *> embedded;
    for (KitAspect * const aspect : std::as_const(m_kitAspects)) {
        QList<KitAspect *> embeddables;
        for (const QList<Id> embeddableIds = aspect->factory()->embeddableAspects();
             const Id &embeddableId : embeddableIds) {
            if (KitAspect * const embeddable = aspectsById.value(embeddableId)) {
                embeddables << embeddable;
                embedded << embeddable;
            }
        }
        aspect->setAspectsToEmbed(embeddables);
    }

    // What is embedded is part of the row that shows it, not a row of its own.
    for (KitAspect * const aspect : std::as_const(m_kitAspects)) {
        if (!embedded.contains(aspect))
            m_kitAspectRows.registerAspect(aspect);
    }
}

void KitOptionsPageWidget::updateVisibility()
{
    for (KitAspect *aspect : std::as_const(m_kitAspects))
        aspect->setVisible(m_model.modifiedKit()->isAspectRelevant(aspect->factory()->id()));
}

// What the button offers: the default icon of every device kind that has one,
// a file to pick, and the way back to the device default. The last was on the
// button's context menu; there is one menu now.
static const char BROWSE_ICON_ID[] = "PE.Kits.BrowseIcon";
static const char RESET_ICON_ID[] = "PE.Kits.ResetIcon";

void KitOptionsPageWidget::refreshIconChoices()
{
    const Id deviceType = RunDeviceTypeKitAspect::deviceTypeId(m_model.modifiedKit());
    QList<IDeviceFactory *> allDeviceFactories = IDeviceFactory::allDeviceFactories();
    if (deviceType.isValid()) {
        // The kind the kit builds for comes first.
        const auto less = [deviceType](const IDeviceFactory *f1, const IDeviceFactory *f2) {
            if (f1->deviceType() == deviceType)
                return true;
            if (f2->deviceType() == deviceType)
                return false;
            return f1->displayName() < f2->displayName();
        };
        Utils::sort(allDeviceFactories, less);
    }

    QList<AspectPresentation::Choice> choices;
    for (const IDeviceFactory * const factory : std::as_const(allDeviceFactories)) {
        if (factory->icon().isNull())
            continue;
        choices.append({Tr::tr("Default for %1").arg(factory->displayName()),
                        {},
                        true,
                        factory->deviceType().toSetting(),
                        factory->icon()});
    }
    choices.append({PathChooser::browseButtonLabel(), {}, true, QString(BROWSE_ICON_ID)});
    choices.append({Tr::tr("Reset to Device Default Icon"), {}, true, QString(RESET_ICON_ID)});

    m_iconButton.setChoices(choices);
    m_iconButton.setActionIcon(m_model.modifiedKit()->icon());
}

void KitOptionsPageWidget::chooseIcon(const QVariant &choice)
{
    const QString id = choice.toString();
    if (id == QLatin1String(RESET_ICON_ID)) {
        m_model.modifiedKit()->setIconPath({});
    } else if (id == QLatin1String(BROWSE_ICON_ID)) {
        const FilePath path = FileUtils::getOpenFilePath(Tr::tr("Select Icon"),
                                                         m_model.modifiedKit()->iconPath(),
                                                         Tr::tr("Images (*.png *.xpm *.jpg)"));
        if (path.isEmpty())
            return;
        if (QIcon(path.toUrlishString()).isNull())
            return;
        m_model.modifiedKit()->setIconPath(path);
    } else {
        m_model.modifiedKit()->setDeviceTypeForIcon(Id::fromSetting(choice));
    }
    refreshIconChoices();
    onDirty();
}

void KitOptionsPageWidget::setDisplayName()
{
    // The cursor stays where it is: an aspect writes back to the control only
    // when the value differs from what the control already shows.
    m_model.modifiedKit()->setUnexpandedDisplayName(m_nameEdit.volatileValue());
    if (!m_loading)
        onDirty();
}

void KitOptionsPageWidget::setFileSystemFriendlyName()
{
    const QString name = m_fileSystemFriendlyNameLineEdit.volatileValue();
    if (name == m_model.modifiedKit()->customFileSystemFriendlyName())
        return;
    m_model.modifiedKit()->setCustomFileSystemFriendlyName(name);
    if (!m_loading)
        onDirty();
}

void KitOptionsPageWidget::workingCopyWasUpdated(Kit *k)
{
    if (k != m_model.modifiedKit() || m_fixingKit || m_loading)
        return;

    m_fixingKit = true;
    k->fix();
    m_fixingKit = false;

    for (KitAspect *w : std::as_const(m_kitAspects))
        w->refresh();

    if (k->unexpandedDisplayName() != m_nameEdit.volatileValue())
        m_nameEdit.setValue(k->unexpandedDisplayName());

    m_fileSystemFriendlyNameLineEdit.setValue(k->customFileSystemFriendlyName());
    refreshIconChoices();
    updateVisibility();
    onDirty();
}

// KitsSettingsPage

class KitsSettingsPage : public Core::IOptionsPage
{
public:
    KitsSettingsPage()
    {
        setId(Constants::KITS_SETTINGS_PAGE_ID);
        setDisplayName(Tr::tr("Kits"));
        setCategory(Constants::KITS_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<Internal::KitOptionsPageWidget> theAspects;
            return theAspects.get();
        });
    }
};

void setupKitsSettingsPage()
{
    static KitsSettingsPage theKitsSettingsPage;
}

} // ProjectExplorer::Internal

#ifdef WITH_TESTS

#include <QTest>

namespace ProjectExplorer::Internal {

static Kit *addTestKit(const QString &name)
{
    return KitManager::registerKit([&name](Kit *k) {
        k->setUnexpandedDisplayName(name);
    });
}

class KitModelTest : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultSuffix();
    void testCancelDoesNotPersistDefault();
    void testRemoveDefaultAutoSwitches();
    void testApplyCommitsDefault();
    void testApplyWithRemovedKit();
};

void KitModelTest::testDefaultSuffix()
{
    const DirtySettingsGuard guard;
    Kit *kit = addTestKit("SuffixKit");
    KitManager::setDefaultKit(kit);

    KitModel model;
    const int row = model.rowForOriginalKit(kit);
    QVERIFY(row >= 0);
    QVERIFY(model.isDefault(row));
    const QString text = model.data(model.index(row, 0), Qt::DisplayRole).toString();
    QVERIFY2(text.contains("(Default)"), qPrintable(text));

    KitManager::deregisterKit(kit);
}

void KitModelTest::testCancelDoesNotPersistDefault()
{
    const DirtySettingsGuard guard;
    Kit *kit1 = addTestKit("CancelKit1");
    Kit *kit2 = addTestKit("CancelKit2");
    KitManager::setDefaultKit(kit1);

    {
        KitModel model;
        const int row2 = model.rowForOriginalKit(kit2);
        QVERIFY(row2 >= 0);
        model.setVolatileDefaultRow(row2);
        // Destroy model without apply() -- simulates Cancel
    }

    QCOMPARE(KitManager::defaultKit(), kit1);

    KitManager::deregisterKit(kit1);
    KitManager::deregisterKit(kit2);
}

void KitModelTest::testRemoveDefaultAutoSwitches()
{
    const DirtySettingsGuard guard;
    Kit *kit1 = addTestKit("RemoveKit1");
    Kit *kit2 = addTestKit("RemoveKit2");
    KitManager::setDefaultKit(kit1);

    KitModel model;
    const int row1 = model.rowForOriginalKit(kit1);
    QVERIFY(row1 >= 0);
    QVERIFY(model.isDefault(row1));

    model.markRemoved(row1);

    QVERIFY(model.isRemoved(row1));
    const int newDefault = model.defaultRow();
    QVERIFY(newDefault >= 0);
    QVERIFY(newDefault != row1);
    QVERIFY(!model.isRemoved(newDefault));

    model.apply();

    QList<Kit *> kits = KitManager::kits();
    QVERIFY(!kits.contains(kit1));
    QVERIFY(kits.contains(kit2));

    KitManager::deregisterKit(kit2);
}

void KitModelTest::testApplyCommitsDefault()
{
    const DirtySettingsGuard guard;
    Kit *kit1 = addTestKit("ApplyKit1");
    Kit *kit2 = addTestKit("ApplyKit2");
    KitManager::setDefaultKit(kit1);

    KitModel model;
    const int row2 = model.rowForOriginalKit(kit2);
    QVERIFY(row2 >= 0);

    model.setVolatileDefaultRow(row2);
    model.apply();

    QCOMPARE(KitManager::defaultKit(), kit2);

    KitManager::deregisterKit(kit1);
    KitManager::deregisterKit(kit2);
}

void KitModelTest::testApplyWithRemovedKit()
{
    // Regression test for QTCREATORBUG-34340: Apply while a kit is removed
    // must not leave rows with null kit pointers visible during the model reset.
    const DirtySettingsGuard guard;
    Kit *kit1 = addTestKit("RemoveApplyKit1");
    Kit *kit2 = addTestKit("RemoveApplyKit2");

    KitModel model;
    const int row1 = model.rowForOriginalKit(kit1);
    QVERIFY(row1 >= 0);
    model.markRemoved(row1);
    QVERIFY(model.isRemoved(row1));

    bool checkedDuringReset = false;
    QObject::connect(model.groupedDisplayModel(), &QAbstractItemModel::modelReset,
                     &model, [&model, &checkedDuringReset] {
        checkedDuringReset = true;
        for (int row = 0; row < model.itemCount(); ++row)
            QVERIFY(model.kitForRow(row));
    });

    const Id kit1Id = kit1->id();
    const int countBeforeApply = model.itemCount();
    model.apply();

    QVERIFY(checkedDuringReset);
    QCOMPARE(model.itemCount(), countBeforeApply - 1);
    QCOMPARE(model.rowForId(kit1Id), -1);
    QVERIFY(model.rowForOriginalKit(kit2) >= 0);
    QVERIFY(!KitManager::kits().contains(kit1));
    QVERIFY(KitManager::kits().contains(kit2));
    KitManager::deregisterKit(kit2);
}

QObject *createKitModelTest()
{
    return new KitModelTest;
}

} // ProjectExplorer::Internal

#endif // WITH_TESTS

#include "kitoptionspage.moc"

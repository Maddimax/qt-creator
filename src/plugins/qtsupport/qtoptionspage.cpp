// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtoptionspage.h"

#include "qtsupportconstants.h"
#include "qtsupporttr.h"
#include "qtsupportutils.h"
#include "qtversionmanager.h"
#include "qtversionfactory.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/deviceselectionaspect.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/kitaspect.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/toolchainmanager.h>

#include <utils/algorithm.h>
#include <utils/filedialogs.h>
#include <utils/fileutils.h>
#include <utils/groupedlistaspect.h>
#include <utils/groupedmodel.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/shutdownguard.h>
#include <utils/treemodel.h>
#include <utils/utilsicons.h>

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeView>

#include <memory>
#include <utility>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

const char kInstallSettingsKey[] = "Settings/InstallSettings";

namespace QtSupport::Internal {

static QIcon invalidVersionIcon()
{
    return Utils::Icons::CRITICAL.icon();
}

static QIcon warningVersionIcon()
{
    return Utils::Icons::WARNING.icon();
}

static QIcon validVersionIcon()
{
    return {};
}

static QString nonUniqueDisplayNameWarning()
{
    return Tr::tr("Display Name is not unique.");
}

struct UnsupportedAbisInfo
{
    enum class Status { Ok, SomeMissing, AllMissing } status = Status::Ok;
    QString message;
};

static UnsupportedAbisInfo checkForUnsupportedAbis(const QtVersion *version)
{
    Abis missingToolchains;
    const Abis qtAbis = version->qtAbis();

    for (const Abi &abi : qtAbis) {
        const auto abiComparePred = [&abi] (const Toolchain *tc) {
            return Utils::contains(tc->supportedAbis(),
                                   [&abi](const Abi &sabi) { return sabi.isCompatibleWith(abi); });
        };

        if (!ToolchainManager::toolchain(abiComparePred))
            missingToolchains.append(abi);
    }

    UnsupportedAbisInfo info;
    if (!missingToolchains.isEmpty()) {
        const auto formatAbiHtmlList = [](const Abis &abis) {
            QString result = QStringLiteral("<ul><li>");
            for (int i = 0, count = abis.size(); i < count; ++i) {
                if (i)
                    result += QStringLiteral("</li><li>");
                result += abis.at(i).toString();
            }
            result += QStringLiteral("</li></ul>");
            return result;
        };

        if (missingToolchains.count() == qtAbis.size()) {
            info.status = UnsupportedAbisInfo::Status::AllMissing;
            info.message =
                Tr::tr("No compiler can produce code for this Qt version."
                       " Please define one or more compilers for: %1").arg(formatAbiHtmlList(qtAbis));
        } else {
            info.status = UnsupportedAbisInfo::Status::SomeMissing;
            info.message = Tr::tr("The following ABIs are currently not supported: %1")
                               .arg(formatAbiHtmlList(missingToolchains));
        }
    }

    return info;
}

QVariant qtVersionData(const QtVersion *version, int column, int role, bool hasNonUniqueName)
{
    if (!version) {
        if (role == KitAspect::IsNoneRole && column == 0)
            return true;
        if (role == Qt::DisplayRole && column == 0)
            return Tr::tr("None", "No Qt version");
        if (role == KitAspect::IdRole)
            return -1;
        return {};
    }

    if (role == FilePathRole)
        return version->qtFilePath().toVariant();

    if (!version->isVersionInfoAvailable()) {
        // The qmake query is still running in the background (e.g. on a slow remote
        // device). Render a lightweight placeholder without calling the blocking getters;
        // the row is refreshed once the information arrives (see refreshLoadingRows()).
        if (role == Qt::DisplayRole) {
            if (column == 0)
                return Tr::tr("%1 (reading information...)").arg(version->unexpandedDisplayName());
            if (column == 1)
                return version->qtFilePath().toUserOutput();
        }
        if (role == KitAspect::IdRole)
            return version->uniqueId();
        return {};
    }

    if (role == Qt::DisplayRole) {
        if (column == 0)
            return version->displayName();
        if (column == 1)
            return version->qtFilePath().toUserOutput();
    }

    // Bad < Limited < Good, keep sorted ascending.
    enum class Quality { Bad, Limited, Good };
    const auto computeQuality = [&]() -> Quality {
        if (!version->isValid())
            return Quality::Bad;
        if (!version->warningReason().isEmpty() || hasNonUniqueName)
            return Quality::Limited;
        const UnsupportedAbisInfo abisInfo = checkForUnsupportedAbis(version);
        switch (abisInfo.status) {
        case UnsupportedAbisInfo::Status::AllMissing:
            return Quality::Bad;
        case UnsupportedAbisInfo::Status::SomeMissing:
            return Quality::Limited;
        case UnsupportedAbisInfo::Status::Ok:
            break;
        }
        return Quality::Good;
    };

    if (role == Qt::DecorationRole && column == 0) {
        switch (computeQuality()) {
        case Quality::Good:
            return validVersionIcon();
        case Quality::Limited:
            return warningVersionIcon();
        case Quality::Bad:
            return invalidVersionIcon();
        }
    }

    if (role == Qt::ToolTipRole) {
        QString desc = version->toHtml(true);
        QString extra;
        const QString row = "<tr><td><b>%1:</b></td><td>%2</td></tr>";
        if (version->isValid()) {
            const UnsupportedAbisInfo abisInfo = checkForUnsupportedAbis(version);
            if (abisInfo.status == UnsupportedAbisInfo::Status::AllMissing)
                extra += row.arg(Tr::tr("Error"), abisInfo.message);
            else if (abisInfo.status == UnsupportedAbisInfo::Status::SomeMissing)
                extra += row.arg(Tr::tr("Warning"), abisInfo.message);
            for (const QString &w : version->warningReason())
                extra += row.arg(Tr::tr("Warning"), w);
        }
        if (hasNonUniqueName)
            extra += row.arg(Tr::tr("Warning"), nonUniqueDisplayNameWarning());
        if (!extra.isEmpty())
            desc.replace("</table>", extra + "</table>");
        return desc;
    }

    if (role == KitAspect::IdRole)
        return version->uniqueId();

    if (role == KitAspect::QualityRole)
        return int(computeQuality());

    return {};
}

QtVersionItem::QtVersionItem(QtVersion *version) : m_version(version) {}

QVariant QtVersionItem::data(int column, int role) const
{
    return qtVersionData(version(), column, role, hasNonUniqueDisplayName());
}

int QtVersionItem::uniqueId() const
{
    return m_version ? m_version->uniqueId() : -1;
}

} // namespace QtSupport::Internal

Q_DECLARE_METATYPE(QtSupport::Internal::QtVersionItem)

namespace QtSupport {
namespace Internal {

// QtVersionModel

class QtVersionModel : public TypedGroupedModel<QtVersionItem>
{
public:
    QtVersionModel()
    {
        setHeader({Tr::tr("Name"), Tr::tr("qmake Path")});
        setFilters(ProjectExplorer::Constants::msgAutoDetected(),
                   {{ProjectExplorer::Constants::msgManual(), [this](int row) {
                       const QtVersionItem it = this->item(row);
                       return it.version() && !it.version()->detectionSource().isAutoDetected();
                   }}});
    }

    void destroyRow(int row)
    {
        if (row < 0 || isRemoved(row))
            return;
        markRemoved(row);
    }

    int cloneRow(int row) override
    {
        const QtVersionItem &it = item(row);
        if (!it.version())
            return -1;
        QtVersion *clone = QtVersionFactory::createQtVersionFromQMakePath(
            it.version()->qtFilePath(), DetectionSource::Manual);
        if (!clone)
            return -1;
        clone->setUnexpandedDisplayName(Tr::tr("Clone of %1").arg(it.version()->displayName()));
        return appendVolatileItem(QtVersionItem(clone));
    }

    QVariant variantData(int row, int column, int role) const final
    {
        return item(row).data(column, role);
    }
};

// QtSettingsPageWidget

// The qmake path of the version being looked at, and the button that changes
// it: picking a different qmake replaces the version rather than editing a
// field, so it is a summary with an action and not a path chooser.
class QmakePathAspect final : public BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::TextWithAction;
        p.actionText = PathChooser::browseButtonLabel();
        return p;
    }

    QString displayText() const override { return m_path; }

    void setPath(const QString &path)
    {
        if (path == m_path)
            return;
        m_path = path;
        emit displayTextChanged();
    }

    void triggerAction() override { emit actionTriggered(); }

signals:
    void actionTriggered();

private:
    QString m_path;
};

class QtSettingsPageWidget final : public AspectContainer
{
    Q_OBJECT

public:
    QtSettingsPageWidget();
    ~QtSettingsPageWidget() final;

    static void linkWithQt();

    void apply() override;
    void cancel() override;

    bool isDirty() const override
    {
        return m_model.isDirty() || m_documentationSetting.volatileValue() != m_initialDocumentation;
    }

    DeviceSelectionAspect m_deviceComboBox{this};
    GroupedListAspect m_versions{this};
    ActionAspect m_addButton{this};
    ActionAspect m_redetectButton{this};
    ActionAspect m_linkWithQtButton{this};
    ActionAspect m_cleanUpButton{this};
    SelectionAspect m_documentationSetting{this};

    AspectContainer m_details{this};
    StringAspect m_nameEdit{&m_details};
    QmakePathAspect m_qmakePath{&m_details};
    TextDisplay m_errorLabel{&m_details};
    TextDisplay m_description{&m_details};
    BoolAspect m_showDetails{&m_details};
    TextDisplay m_infoBrowser{&m_details};

    // The extra settings of the version being looked at - QNX's SDP path is
    // the only kind there is - which change with the version.
    ContainerAspect m_configuration{&m_details};

private:
    void updateDescriptionLabel();
    void userChangedCurrentVersion();
    void updateWidgets();
    void updateButtons();
    void updateLinkWithQtButton();
    QtVersion *currentVersion() const;
    std::pair<bool, QString> checkAlreadyExists(const FilePath &qtVersion);

    void updateQtVersions(const QList<int> &, const QList<int> &, const QList<int> &);
    void scheduleVersionInfoUpdates();
    void refreshLoadingRows();
    void addQtDir();
    void redetect();
    void editPath();
    void updateCleanUpButton();
    void updateCurrentQtName();

    void cleanUpQtVersions();
    void toolChainsUpdated();

    void setInfoWidgetVisibility();
    void infoAnchorClicked(const QUrl &);

    struct ValidityInfo {
        QString description;
        QString message;
        QString toolTip;
    };
    ValidityInfo validInformation(const QtVersion *version);
    QList<ProjectExplorer::Toolchain*> toolChains(const QtVersion *version);
    Id defaultToolchainId(const QtVersion *version);

    bool isNameUnique(const QtVersion *version);

    int m_initialDocumentation = 0;
    QString m_loadedVersionName;
    bool m_preEditChanged = false;
    bool m_applyingVersions = false;
    QtVersionModel m_model;

    // Rows whose qmake query is still running; polled so the row is refreshed once its
    // version information becomes available instead of blocking the GUI on open.
    QSet<int> m_pendingInfoIds;
    QTimer m_infoPollTimer;
};

QtSettingsPageWidget::QtSettingsPageWidget()
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/QtSupport/QtVersionsPage.qml"));

    m_deviceComboBox.setQmlName("Device");

    m_versions.setQmlName("Versions");
    m_versions.setModel(&m_model);
    // An auto-detected Qt version is not the user's to take away.
    m_versions.setCanRemoveRow([this](int row) {
        const QtVersion *version = m_model.item(row).version();
        return version && !version->detectionSource().isAutoDetected();
    });

    m_addButton.setQmlName("Add");
    m_addButton.setActionText(Tr::tr("Add..."));
    m_addButton.setAction([this] { addQtDir(); });

    m_redetectButton.setQmlName("Redetect");
    m_redetectButton.setActionText(Tr::tr("Re-detect"));
    m_redetectButton.setAction([this] { redetect(); });

    m_linkWithQtButton.setQmlName("LinkWithQt");
    m_linkWithQtButton.setActionText(Tr::tr("Link with Qt..."));
    m_linkWithQtButton.setAction([] { LinkWithQtSupport::linkWithQt(); });

    m_cleanUpButton.setQmlName("CleanUp");
    m_cleanUpButton.setActionText(Tr::tr("Clean Up"));
    m_cleanUpButton.setAction([this] { cleanUpQtVersions(); });

    m_documentationSetting.setQmlName("Documentation");
    m_documentationSetting.setLabelText(Tr::tr("Register documentation:"));
    m_documentationSetting.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    // In DocumentationSetting's order, so the index is the enumerator.
    m_documentationSetting.addOption(Tr::tr("Highest Version Only"));
    m_documentationSetting.addOption(Tr::tr("All", "All documentation"));
    m_documentationSetting.addOption(Tr::tr("None", "No documentation"));
    m_documentationSetting.setValue(int(QtVersionManager::documentationSetting()));
    m_initialDocumentation = m_documentationSetting.volatileValue();

    m_details.setQmlName("Details");
    m_configuration.setQmlName("Configuration");

    m_nameEdit.setQmlName("Name");
    m_nameEdit.setLabelText(Tr::tr("Name:"));
    m_nameEdit.setDisplayStyle(StringAspect::LineEditDisplay);

    m_qmakePath.setQmlName("QmakePath");
    m_qmakePath.setLabelText(Tr::tr("qmake path:"));

    m_errorLabel.setQmlName("Error");
    m_errorLabel.setIconType(InfoType::Error);
    m_errorLabel.setWordWrap(true);

    m_description.setQmlName("Description");

    m_showDetails.setQmlName("ShowDetails");
    m_showDetails.setLabelText(Tr::tr("Details"));

    m_infoBrowser.setQmlName("Info");
    m_infoBrowser.setTextFormat(AspectControls::TextFormat::RichText);
    m_infoBrowser.setWordWrap(true);
    connect(&m_infoBrowser, &TextDisplay::linkActivated,
            this, [](const QString &link) { QDesktopServices::openUrl(QUrl(link)); });

    // Behaviour, not layout.
    connect(&m_versions, &GroupedListAspect::currentRowChanged,
            this, [this] { userChangedCurrentVersion(); });
    connect(&m_versions, &GroupedListAspect::currentRemoved,
            this, [this] { updateButtons(); });
    m_nameEdit.addOnVolatileValueChanged(this, [this] { updateCurrentQtName(); });
    connect(&m_qmakePath, &QmakePathAspect::actionTriggered, this, [this] { editPath(); });
    connect(&m_showDetails, &BaseAspect::volatileValueChanged,
            this, [this] { setInfoWidgetVisibility(); });
    connect(&m_deviceComboBox, &DeviceSelectionAspect::currentDeviceChanged,
            this, [this] { updateLinkWithQtButton(); });

    QList<int> additions = transform(QtVersionManager::versions(), &QtVersion::uniqueId);
    updateQtVersions(additions, QList<int>(), QList<int>());

    userChangedCurrentVersion();

    connect(QtVersionManager::instance(), &QtVersionManager::qtVersionsChanged,
            this, &QtSettingsPageWidget::updateQtVersions);

    m_infoPollTimer.setInterval(250);
    connect(&m_infoPollTimer, &QTimer::timeout,
            this, &QtSettingsPageWidget::refreshLoadingRows);
    connect(ProjectExplorer::ToolchainManager::instance(), &ToolchainManager::toolchainsChanged,
            this, &QtSettingsPageWidget::toolChainsUpdated);

    // The name may name the version's own variables, so the field expands
    // against whichever version is being looked at.
    m_nameEdit.setMacroExpander(nullptr);

    connect(&m_deviceComboBox, &DeviceSelectionAspect::currentDeviceChanged, this, [this] {
        const IDeviceConstPtr dev = m_deviceComboBox.currentDevice();
        const FilePath deviceRoot = dev ? dev->rootPath() : FilePath();
        m_model.setExtraFilter(deviceRoot.isEmpty()
            ? GroupedModel::Filter{}
            : GroupedModel::Filter{[this, deviceRoot](int row) {
                  const QtVersionItem it = m_model.item(row);
                  const FilePath path = it.version() ? it.version()->qtFilePath() : FilePath{};
                  return path.isEmpty() || path.isSameDevice(deviceRoot);
              }});
    });
    updateLinkWithQtButton();
}

QtVersion *QtSettingsPageWidget::currentVersion() const
{
    const int row = m_versions.currentRow();
    if (row < 0)
        return nullptr;
    return m_model.item(row).version();
}


std::pair<bool, QString> QtSettingsPageWidget::checkAlreadyExists(const FilePath &qtVersion)
{
    for (int row = 0; row < m_model.itemCount(); ++row) {
        const QtVersionItem it = m_model.item(row);
        if (!it.version())
            continue;
        const FilePath &itemPath = it.version()->qtFilePath();
        if (itemPath.isSameExecutable(qtVersion)
            || (itemPath.parentDir() == qtVersion.parentDir()
                && itemPath.fileSize() == qtVersion.fileSize())) {
            return {true, it.version()->displayName()};
        }
    }
    return {false, {}};
}

void QtSettingsPageWidget::cleanUpQtVersions()
{
    QList<int> rowsToRemove;
    QString text;

    for (int row = 0; row < m_model.itemCount(); ++row) {
        if (m_model.isRemoved(row))
            continue;
        if (QtVersion *version = m_model.item(row).version()) {
            if (version->detectionSource().isAutoDetected())
                continue;
            if (!version->isValid()) {
                rowsToRemove.prepend(row);
                if (!text.isEmpty())
                    text.append(QLatin1String("</li><li>"));
                text.append(version->displayName());
            }
        }
    }

    if (rowsToRemove.isEmpty())
        return;

    if (QMessageBox::warning(nullptr, Tr::tr("Remove Invalid Qt Versions"),
                             Tr::tr("Do you want to remove all invalid Qt Versions?<br>"
                                    "<ul><li>%1</li></ul><br>"
                                    "will be removed.").arg(text),
                             QMessageBox::Yes, QMessageBox::No) == QMessageBox::No)
        return;

    for (int row : std::as_const(rowsToRemove))
        m_model.destroyRow(row);

    updateCleanUpButton();
}

void QtSettingsPageWidget::toolChainsUpdated()
{
    const int curRow = m_versions.currentRow();
    for (int row = 0; row < m_model.itemCount(); ++row) {
        if (row == curRow)
            updateDescriptionLabel();
        else
            m_model.notifyRowChanged(row);
    }
}

void QtSettingsPageWidget::setInfoWidgetVisibility()
{
    const bool expanded = m_showDetails.volatileValue();
    if (expanded && m_infoBrowser.text().isEmpty()) {
        if (const QtVersion *version = currentVersion())
            m_infoBrowser.setText(version->toHtml(true));
    }
    m_infoBrowser.setVisible(expanded);
}

void QtSettingsPageWidget::infoAnchorClicked(const QUrl &url)
{
    QDesktopServices::openUrl(url);
}

QtSettingsPageWidget::ValidityInfo QtSettingsPageWidget::validInformation(const QtVersion *version)
{
    ValidityInfo info;

    if (!version)
        return info;

    info.description = Tr::tr("Qt version %1 for %2").arg(version->qtVersionString(), version->description());
    if (!version->isValid()) {
        info.message = version->invalidReason();
        return info;
    }

    bool useable = true;
    QStringList warnings;
    if (!isNameUnique(version))
        warnings << nonUniqueDisplayNameWarning();

    const UnsupportedAbisInfo unsupportedAbisInfo = checkForUnsupportedAbis(version);
    if (unsupportedAbisInfo.status == UnsupportedAbisInfo::Status::AllMissing) {
        info.message = unsupportedAbisInfo.message;
        useable = false;
    } else if (unsupportedAbisInfo.status == UnsupportedAbisInfo::Status::SomeMissing) {
        warnings << Tr::tr(
            "Not all possible target environments can be supported due to missing compilers.");
        info.toolTip = unsupportedAbisInfo.message;
    }

    if (useable) {
        warnings += version->warningReason();
        if (!warnings.isEmpty())
            info.message = warnings.join(QLatin1Char('\n'));
    }

    return info;
}

QList<Toolchain*> QtSettingsPageWidget::toolChains(const QtVersion *version)
{
    QList<Toolchain*> toolChains;
    if (!version)
        return toolChains;

    QSet<Id> ids;
    const Abis abis = version->qtAbis();
    for (const Abi &a : abis) {
        const Toolchains tcList = ToolchainManager::findToolchains(a);
        for (Toolchain *tc : tcList) {
            if (Utils::insert(ids, tc->id()))
                toolChains.append(tc);
        }
    }

    return toolChains;
}

Id QtSettingsPageWidget::defaultToolchainId(const QtVersion *version)
{
    QList<Toolchain*> possibleToolChains = toolChains(version);
    if (!possibleToolChains.isEmpty())
        return possibleToolChains.first()->id();
    return {};
}

bool QtSettingsPageWidget::isNameUnique(const QtVersion *version)
{
    const QString name = version->displayName().trimmed();

    for (int row = 0; row < m_model.itemCount(); ++row) {
        if (m_model.isRemoved(row))
            continue;
        QtVersion *v = m_model.item(row).version();
        if (v != version && v->displayName().trimmed() == name)
            return false;
    }
    return true;
}

void QtSettingsPageWidget::updateQtVersions(const QList<int> &additions, const QList<int> &removals,
                                            const QList<int> &changes)
{
    if (m_applyingVersions)
        return;
    QList<int> toAdd = additions;

    // Find existing rows to remove/change (descending to keep indices stable):
    QList<int> rowsToRemove;
    for (int row = m_model.itemCount() - 1; row >= 0; --row) {
        const int id = m_model.item(row).uniqueId();
        if (removals.contains(id)) {
            rowsToRemove.append(row);
        } else if (changes.contains(id)) {
            toAdd.append(id);
            rowsToRemove.append(row);
        }
    }

    // Remove changed/removed items:
    for (int row : std::as_const(rowsToRemove))
        m_model.destroyRow(row);

    // Add changed/added items:
    for (int a : std::as_const(toAdd)) {
        QtVersionItem item(QtVersionManager::version(a)->clone());
        item.setIsNameUnique([this](QtVersion *v) { return isNameUnique(v); });
        m_model.appendItem(item);
    }

    m_model.notifyAllRowsChanged();

    scheduleVersionInfoUpdates();
}

void QtSettingsPageWidget::scheduleVersionInfoUpdates()
{
    m_pendingInfoIds.clear();
    for (int row = 0; row < m_model.itemCount(); ++row) {
        QtVersion *v = m_model.item(row).version();
        if (!v)
            continue;
        v->ensureVersionInfoUpdated();
        if (!v->isVersionInfoAvailable())
            m_pendingInfoIds.insert(v->uniqueId());
    }
    if (m_pendingInfoIds.isEmpty())
        m_infoPollTimer.stop();
    else if (!m_infoPollTimer.isActive())
        m_infoPollTimer.start();
}

void QtSettingsPageWidget::refreshLoadingRows()
{
    const int currentRow = m_versions.currentRow();
    bool currentRefreshed = false;
    for (int row = 0; row < m_model.itemCount(); ++row) {
        QtVersion *v = m_model.item(row).version();
        if (!v)
            continue;
        const int id = v->uniqueId();
        if (m_pendingInfoIds.contains(id) && v->isVersionInfoAvailable()) {
            m_pendingInfoIds.remove(id);
            m_model.notifyRowChanged(row);
            if (row == currentRow)
                currentRefreshed = true;
        }
    }
    if (currentRefreshed)
        updateDescriptionLabel();
    if (m_pendingInfoIds.isEmpty())
        m_infoPollTimer.stop();
}

QtSettingsPageWidget::~QtSettingsPageWidget() = default;

void QtSettingsPageWidget::addQtDir()
{
    FilePath initialDir;
    const IDeviceConstPtr dev = m_deviceComboBox.currentDevice();
    if (dev && dev->id() != ProjectExplorer::Constants::DESKTOP_DEVICE_ID)
        initialDir = dev->rootPath();
    FilePath qtVersion = FileUtils::getOpenFilePath(
        Tr::tr("Select a qtpaths or qmake Executable"),
        initialDir,
        filterForQmakeFileDialog(initialDir.osType()),
        nullptr,
        QFileDialog::DontResolveSymlinks,
        true);

    if (qtVersion.isEmpty())
        return;

    // should add all qt versions here ?
    if (isQtChooser(qtVersion))
        qtVersion = qtChooserToQmakePath(qtVersion.symLinkTarget());

    bool alreadyExists;
    QString otherName;
    std::tie(alreadyExists, otherName) = checkAlreadyExists(qtVersion);

    if (alreadyExists) {
        // Already exist
        QMessageBox::warning(ICore::dialogParent(), Tr::tr("Qt Version Already Known"),
                             Tr::tr("This Qt version was already registered as \"%1\".")
                             .arg(otherName));
        return;
    }

    QString error;
    QtVersion *version = QtVersionFactory::createQtVersionFromQMakePath(qtVersion, DetectionSource::Manual, &error);
    if (version) {
        QtVersionItem item(version);
        item.setIsNameUnique([this](QtVersion *v) { return isNameUnique(v); });
        m_versions.setCurrentRow(m_model.appendVolatileItem(item));
    } else {
        const QString qtFileName = qtVersion.fileName();
        QMessageBox::warning(
            ICore::dialogParent(),
            Tr::tr("%1 Not Executable").arg(qtFileName),
            Tr::tr("The %1 executable %2 could not be added: %3")
                .arg(qtFileName)
                .arg(qtVersion.toUserOutput())
                .arg(error));
        return;
    }
    updateCleanUpButton();
}

void QtSettingsPageWidget::redetect()
{
    for (const IDeviceConstPtr &dev : m_deviceComboBox.selectedDevices()) {
        const FilePaths qMakes = findQtsInPaths(dev->toolSearchPaths());
        for (const FilePath &qmakePath : qMakes) {
            if (isQtChooser(qmakePath))
                continue;
            if (checkAlreadyExists(qmakePath).first)
                continue;

            if (QtVersion *version = QtVersionFactory::createQtVersionFromQMakePath(
                    qmakePath, {DetectionSource::Manual, "PATH"})) {
                QtVersionItem item(version);
                item.setIsNameUnique([this](QtVersion *v) { return isNameUnique(v); });
                m_model.appendVolatileItem(item);
            }
        }
    }

    updateCleanUpButton();
}

void QtSettingsPageWidget::editPath()
{
    const int row = m_versions.currentRow();
    QTC_ASSERT(row >= 0, return);
    QtVersion *current = m_model.item(row).version();
    QTC_ASSERT(current, return);
    FilePath qtVersion = FileUtils::getOpenFilePath(
        Tr::tr("Select a qtpaths or qmake Executable"),
        current->qtFilePath().absolutePath(),
        filterForQmakeFileDialog(current->qtFilePath().osType()),
        nullptr,
        QFileDialog::DontResolveSymlinks);
    if (qtVersion.isEmpty())
        return;
    QtVersion *version = QtVersionFactory::createQtVersionFromQMakePath(qtVersion, DetectionSource::Manual, nullptr);
    if (!version)
        return;
    // Same type? then replace!
    if (current->type() != version->type()) {
        // not the same type, error out
        QMessageBox::critical(ICore::dialogParent(), Tr::tr("Incompatible Qt Versions"),
                              Tr::tr("The Qt version selected must match the device type."),
                              QMessageBox::Ok);
        delete version;
        return;
    }
    // same type, replace
    version->setId(current->uniqueId());
    if (current->unexpandedDisplayName() != current->defaultUnexpandedDisplayName())
        version->setUnexpandedDisplayName(current->displayName());

    // Replace the item's version in the model
    QtVersionItem it = m_model.item(row);
    it.m_version = std::shared_ptr<QtVersion>(version);
    m_model.setVolatileItem(row, it);
    m_model.setChanged(row, true);
    userChangedCurrentVersion();
}

// To be called if a Qt version was removed or added
void QtSettingsPageWidget::updateCleanUpButton()
{
    bool hasInvalidVersion = false;
    for (int row = 0; row < m_model.itemCount(); ++row) {
        if (m_model.isRemoved(row))
            continue;
        if (QtVersion *version = m_model.item(row).version()) {
            if (version->detectionSource().isAutoDetected())
                continue;
            if (!version->isValid()) {
                hasInvalidVersion = true;
                break;
            }
        }
    }

    m_cleanUpButton.setEnabled(hasInvalidVersion);
}

void QtSettingsPageWidget::userChangedCurrentVersion()
{
    updateWidgets();
    updateDescriptionLabel();
    updateCleanUpButton();
}

void QtSettingsPageWidget::updateDescriptionLabel()
{
    const int row = m_versions.currentRow();
    const QtVersion *version = row >= 0 ? m_model.item(row).version() : nullptr;

    if (version && !version->isVersionInfoAvailable()) {
        // The qmake query is still running; do not block on validInformation(). The panel
        // is refreshed once the information arrives (see refreshLoadingRows()).
        m_errorLabel.setVisible(false);
        m_description.setText(Tr::tr("Reading Qt version information..."));
        m_description.setVisible(true);
        m_infoBrowser.setText({});
        m_infoBrowser.setVisible(false);
        return;
    }

    const ValidityInfo info = validInformation(version);
    m_errorLabel.setVisible(!info.message.isEmpty());
    if (!info.message.isEmpty()) {
        m_errorLabel.setText(info.message);
        m_errorLabel.setToolTip(info.toolTip);
    }
    m_description.setText(info.description);
    m_description.setVisible(version != nullptr);
    if (row >= 0)
        m_model.notifyRowChanged(row);

    m_infoBrowser.setText({});
    if (version)
        setInfoWidgetVisibility();
    else
        m_infoBrowser.setVisible(false);
}

void QtSettingsPageWidget::updateWidgets()
{
    const int row = m_versions.currentRow();
    QtVersion *version = currentVersion();
    m_loadedVersionName = version ? version->unexpandedDisplayName() : QString{};
    m_preEditChanged = row >= 0 && m_model.isChanged(row);
    if (version) {
        m_nameEdit.setValue(version->unexpandedDisplayName());
        m_qmakePath.setPath(version->qtFilePath().toUserOutput());
        AspectContainer *configuration = version->createConfigurationAspects();
        if (configuration) {
            configuration->setEnabled(!version->detectionSource().isAutoDetected());
            connect(configuration, &AspectContainer::subAspectChanged,
                    this, [this] { updateDescriptionLabel(); });
        }
        m_configuration.setOwnedContainer(configuration);
    } else {
        m_nameEdit.setValue({});
        m_qmakePath.setPath({});
        m_configuration.setOwnedContainer(nullptr);
    }

    updateButtons();
}

void QtSettingsPageWidget::updateButtons()
{
    const QtVersion *version = currentVersion();
    const bool isAutodetected = version && version->detectionSource().isAutoDetected();
    m_nameEdit.setEnabled(version != nullptr);
    m_qmakePath.setEnabled(version && !isAutodetected);
}

static FilePath settingsFile(const QString &baseDir)
{
    return FilePath::fromString(baseDir + (baseDir.isEmpty() ? "" : "/")
                                + QCoreApplication::organizationName() + '/'
                                + QCoreApplication::applicationName() + ".ini");
}

static FilePath qtVersionsFile(const QString &baseDir)
{
    return FilePath::fromString(baseDir + (baseDir.isEmpty() ? "" : "/")
                                + QCoreApplication::organizationName() + '/'
                                + QCoreApplication::applicationName() + '/' + "qtversion.xml");
}

static std::optional<FilePath> currentlyLinkedQtDir(bool *hasInstallSettings)
{
    const FilePath installSettingsFilePath = settingsFile(ICore::resourcePath().path());
    const bool installSettingsExist = installSettingsFilePath.exists();
    if (hasInstallSettings)
        *hasInstallSettings = installSettingsExist;
    if (installSettingsExist) {
        const QVariant value = QSettings(installSettingsFilePath.toFSPathString(), QSettings::IniFormat)
                                   .value(kInstallSettingsKey);
        if (value.isValid())
            return FilePath::fromSettings(value);
    }
    return {};
}

static QString linkingPurposeText()
{
    return Tr::tr(
        "Linking with a Qt installation automatically registers Qt versions and kits, and other "
        "tools that were installed with that Qt installer, in this %1 installation. Other %1 "
        "installations are not affected.").arg(QGuiApplication::applicationDisplayName());
}

static bool canLinkWithQt(QString *toolTip, const IDeviceConstPtr &device)
{
    bool canLink = true;
    bool installSettingsExist;
    const std::optional<FilePath> installSettingsValue = currentlyLinkedQtDir(
        &installSettingsExist);
    QStringList tip;
    tip << linkingPurposeText();
    if (device && device->id() != ProjectExplorer::Constants::DESKTOP_DEVICE_ID) {
        canLink = false;
        tip << Tr::tr("This functionality is only available for the Desktop device.");
    } else {
        if (!ICore::resourcePath().isWritableDir()) {
            canLink = false;
            tip << Tr::tr("%1's resource directory is not writable.")
                       .arg(QGuiApplication::applicationDisplayName());
        }
        const FilePath link = installSettingsValue ? *installSettingsValue : FilePath();
        if (!link.isEmpty())
            tip << Tr::tr("%1 is currently linked to \"%2\".")
                       .arg(QGuiApplication::applicationDisplayName(), link.toUserOutput());
    }
    if (toolTip)
        *toolTip = tip.join("\n\n");
    return canLink;
}

void QtSettingsPageWidget::updateLinkWithQtButton()
{
    QString tip;
    const bool canLink = canLinkWithQt(&tip, m_deviceComboBox.currentDevice());
    m_linkWithQtButton.setEnabled(canLink);
    m_linkWithQtButton.setToolTip(tip);
}

void QtSettingsPageWidget::updateCurrentQtName()
{
    const int row = m_versions.currentRow();
    if (row < 0 || !m_model.item(row).version())
        return;

    m_model.item(row).version()->setUnexpandedDisplayName(m_nameEdit.volatileValue());
    const bool nameChanged = m_nameEdit.volatileValue() != m_loadedVersionName;
    m_model.setChanged(row, nameChanged || m_preEditChanged);

    updateDescriptionLabel();
    m_model.notifyAllRowsChanged();
}

void QtSettingsPageWidget::apply()
{
    AspectContainer::apply();
    m_initialDocumentation = m_documentationSetting.volatileValue();
    QtVersionManager::setDocumentationSetting(
        QtVersionManager::DocumentationSetting(m_documentationSetting.volatileValue()));

    QtVersions versions;
    for (int row = 0; row < m_model.itemCount(); ++row) {
        if (m_model.isRemoved(row))
            continue;
        versions.append(m_model.item(row).version()->clone());
    }
    m_applyingVersions = true;
    QtVersionManager::setNewQtVersions(versions);
    m_applyingVersions = false;

    m_model.apply();
    userChangedCurrentVersion();
}

void QtSettingsPageWidget::cancel()
{
    AspectContainer::cancel();
    m_model.cancel();
}

const QStringList kSubdirsToCheck = {"",
                                     "Tools/sdktool", // macOS
                                     "Tools/sdktool/share/qtcreator", // Windows/Linux
                                     "Qt Creator.app/Contents/Resources",
                                     "Contents/Resources",
                                     "Tools/QtCreator/share/qtcreator",
                                     "share/qtcreator"};

static FilePaths settingsFilesToCheck()
{
    return Utils::transform(kSubdirsToCheck, [](const QString &dir) { return settingsFile(dir); });
}

static FilePaths qtversionFilesToCheck()
{
    return Utils::transform(kSubdirsToCheck, [](const QString &dir) { return qtVersionsFile(dir); });
}

static FilePath settingsDirForQtDir(const FilePath &baseDirectory, const FilePath &qtDir)
{
    const FilePaths dirsToCheck = Utils::transform(kSubdirsToCheck, [qtDir](const QString &dir) {
        return qtDir / dir;
    });
    return Utils::findOrDefault(dirsToCheck, [baseDirectory](const FilePath &dir) {
        return settingsFile(baseDirectory.resolvePath(dir).path()).exists()
            || qtVersionsFile(baseDirectory.resolvePath(dir).path()).exists();
    });
}

static FancyLineEdit::AsyncValidationResult validateQtInstallDir(const QString &input,
                                                                 const FilePath &baseDirectory)
{
    const FilePath qtDir = FilePath::fromUserInput(input);
    if (settingsDirForQtDir(baseDirectory, qtDir).isEmpty()) {
        const FilePaths filesToCheck = settingsFilesToCheck() + qtversionFilesToCheck();
        return make_unexpected(
            "<html><body>"
            + ::QtSupport::Tr::tr("Qt installation information was not found in \"%1\". "
                                  "Choose a directory that contains one of the files %2")
                  .arg(qtDir.toUserOutput(), "<pre>" +
                            filesToCheck.toUserOutput("\n") + "</pre>"));
    }
    return input;
}

static FilePath defaultQtInstallationPath()
{
    if (HostOsInfo::isWindowsHost())
        return FilePath::fromString({"C:/Qt"});
    return FileUtils::homePath() / "Qt";
}

void QtSettingsPageWidget::linkWithQt()
{
    const QString title = Tr::tr("Choose Qt Installation");
    const QString restartText = Tr::tr("The change will take effect after restart.");
    bool askForRestart = false;
    QDialog dialog(ICore::dialogParent());
    dialog.setWindowTitle(title);
    auto tipLabel = new QLabel(linkingPurposeText());
    tipLabel->setWordWrap(true);
    auto pathLabel = new QLabel(Tr::tr("Qt installation path:"));
    pathLabel->setToolTip(
        Tr::tr("Choose the Qt installation directory, or a directory that contains \"%1\".")
            .arg(settingsFile("").toUserOutput()));
    auto pathInput = new PathChooser;
    pathInput->setExpectedKind(PathChooserKind::ExistingDirectory);
    pathInput->setBaseDirectory(FilePath::fromString(QCoreApplication::applicationDirPath()));
    pathInput->setPromptDialogTitle(title);
    pathInput->setMacroExpander(nullptr);
    pathInput->setValidationFunction(
        [pathInput](const QString &input) -> FancyLineEdit::AsyncValidationFuture {
            return pathInput->defaultValidationFunction()(input).then(
                [baseDir = pathInput->baseDirectory()](
                    const FancyLineEdit::AsyncValidationResult &result)
                    -> FancyLineEdit::AsyncValidationResult {
                    if (!result)
                        return result;
                    return validateQtInstallDir(*result, baseDir);
                });
        });
    const std::optional<FilePath> currentLink = currentlyLinkedQtDir(nullptr);
    pathInput->setFilePath(currentLink ? *currentLink : defaultQtInstallationPath());
    pathInput->setAllowPathFromDevice(true);
    auto buttons = new QDialogButtonBox;

    using namespace Layouting;
    Column {
        tipLabel,
        Form {
            Tr::tr("Qt installation path:"), pathInput, br,
        },
        st,
        buttons,
    }.attachTo(&dialog);

    auto linkButton = buttons->addButton(Tr::tr("Link with Qt"), QDialogButtonBox::AcceptRole);
    connect(linkButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto cancelButton = buttons->addButton(Tr::tr("Cancel"), QDialogButtonBox::RejectRole);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto unlinkButton = buttons->addButton(Tr::tr("Remove Link"), QDialogButtonBox::DestructiveRole);
    unlinkButton->setEnabled(currentLink.has_value());
    connect(unlinkButton, &QPushButton::clicked, &dialog, [&dialog, &askForRestart] {
        bool removeSettingsFile = false;
        const FilePath filePath = settingsFile(ICore::resourcePath().path());
        {
            QSettings installSettings(filePath.toFSPathString(), QSettings::IniFormat);
            installSettings.remove(kInstallSettingsKey);
            if (installSettings.allKeys().isEmpty())
                removeSettingsFile = true;
        }
        if (removeSettingsFile)
            filePath.removeFile();
        askForRestart = true;
        dialog.reject();
    });
    connect(pathInput, &PathChooser::validChanged, linkButton, &QPushButton::setEnabled);
    linkButton->setEnabled(pathInput->isValid());

    dialog.setMinimumWidth(520);
    dialog.exec();
    if (dialog.result() == QDialog::Accepted) {
        const FilePath settingsDir = settingsDirForQtDir(pathInput->baseDirectory(),
                                                         pathInput->unexpandedFilePath());
        if (QTC_GUARD(!settingsDir.isEmpty())) {
            const FilePath settingsFilePath = settingsFile(ICore::resourcePath().path());
            QSettings settings(settingsFilePath.toFSPathString(), QSettings::IniFormat);
            settings.setValue(kInstallSettingsKey, settingsDir.toVariant());
            settings.sync();
            if (settings.status() == QSettings::AccessError) {
                QMessageBox::critical(ICore::dialogParent(),
                                      Tr::tr("Error Linking With Qt"),
                                      Tr::tr("Could not write to \"%1\".")
                                        .arg(settingsFilePath.toUserOutput()));
                return;
            }

            askForRestart = true;
        }
    }
    if (askForRestart)
        ICore::askForRestart(restartText);
}

// QtSettingsPage

class QtSettingsPage final : public IOptionsPage
{
public:
    QtSettingsPage()
    {
        setId(Constants::QTVERSION_SETTINGS_PAGE_ID);
        setDisplayName(Tr::tr("Qt Versions"));
        setCategory(ProjectExplorer::Constants::KITS_SETTINGS_CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<QtSettingsPageWidget> theAspects;
            return theAspects.get();
        });
        setFixedKeywords({
            Tr::tr("Add..."),
            Tr::tr("Remove"),
            Tr::tr("Clone"),
            Tr::tr("Clean Up"),
            Tr::tr("Re-detect"),
            Tr::tr("Link with Qt..."),
            Tr::tr("Remove Link"),
            Tr::tr("Name:"),
            Tr::tr("Device:"),
            Tr::tr("Qt installation path:"),
            Tr::tr("qmake path:"),
            Tr::tr("Register documentation:"),
            PathChooser::browseButtonLabel()
        });
    }
};

void setupQtSettingsPage()
{
    static QtSettingsPage theQtSettingsPage;
}

} // namespace Internal

bool LinkWithQtSupport::canLinkWithQt()
{
    return Internal::canLinkWithQt(nullptr, {});
}

bool LinkWithQtSupport::isLinkedWithQt()
{
    return Internal::currentlyLinkedQtDir(nullptr).has_value();
}

Utils::FilePath LinkWithQtSupport::linkedQt()
{
    return Internal::currentlyLinkedQtDir(nullptr).value_or(Utils::FilePath());
}

void LinkWithQtSupport::linkWithQt()
{
    Internal::QtSettingsPageWidget::linkWithQt();
}

} // QtSupport

#include "qtoptionspage.moc"

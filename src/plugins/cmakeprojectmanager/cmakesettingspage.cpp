// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cmakesettingspage.h"

#include "cmakeprojectconstants.h"
#include "cmakeprojectmanagertr.h"
#include "cmaketool.h"
#include "cmaketoolmanager.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/deviceselectionaspect.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/kitaspect.h>
#include <projectexplorer/projectexplorerconstants.h>

#include <utils/groupedlistaspect.h>
#include <utils/groupedmodel.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/shutdownguard.h>
#include <utils/stringutils.h>
#include <utils/treemodel.h> // for FilePathRole
#include <utils/utilsicons.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;
using namespace ProjectExplorer;

namespace CMakeProjectManager::Internal {

class CMakeToolTreeItem
{
public:
    CMakeToolTreeItem() = default;
    explicit CMakeToolTreeItem(const CMakeTool *item);
    CMakeToolTreeItem(
        const QString &name,
        const FilePath &executable,
        const FilePath &qchFile,
        bool autoRun,
        const DetectionSource &detectionSource);

    void updateErrorFlags();
    bool hasError() const;

    Id id() const { return m_id; }
    FilePath executable() const { return m_executable; }
    DetectionSource detectionSource() const { return m_detectionSource; }

    QVariant data(int column, int role) const;

    friend bool operator==(const CMakeToolTreeItem &a, const CMakeToolTreeItem &b)
    {
        return a.m_id == b.m_id
            && a.m_name == b.m_name
            && a.m_executable == b.m_executable
            && a.m_qchFile == b.m_qchFile
            && a.m_detectionSource == b.m_detectionSource
            && a.m_isAutoRun == b.m_isAutoRun;
    }

    Id m_id;
    QString m_name;
    QString m_tooltip;
    FilePath m_executable;
    FilePath m_qchFile;
    QString m_versionDisplay;
    DetectionSource m_detectionSource;
    bool m_isAutoRun = true;
    bool m_isSupported = false;
    bool m_hasError = false;
};

} // namespace CMakeProjectManager::Internal

Q_DECLARE_METATYPE(CMakeProjectManager::Internal::CMakeToolTreeItem)

namespace CMakeProjectManager::Internal {

//
// CMakeToolItemModel
//

class CMakeToolItemModel final : public TypedGroupedModel<CMakeToolTreeItem>
{
public:
    CMakeToolItemModel();

    void addCMakeTool(const CMakeTool *item);
    void updateCMakeTool(const Id &id,
                         const QString &displayName,
                         const FilePath &executable,
                         const FilePath &qchFile);
    void removeCMakeTool(const Id &id);
    void apply() final;

    int cloneRow(int row) override;
    QString uniqueDisplayName(const QString &base) const;
    int rowForId(Id id) const;

private:
    QVariant variantData(int row, int column, int role) const override
    {
        return item(row).data(column, role);
    }
};

CMakeToolTreeItem::CMakeToolTreeItem(const CMakeTool *item)
    : m_id(item->id())
    , m_name(item->displayName())
    , m_executable(item->filePath())
    , m_qchFile(item->qchFilePath())
    , m_versionDisplay(item->versionDisplay())
    , m_detectionSource(item->detectionSource())
    , m_isSupported(item->hasFileApi())
{
    updateErrorFlags();
}

CMakeToolTreeItem::CMakeToolTreeItem(
    const QString &name,
    const FilePath &executable,
    const FilePath &qchFile,
    bool autoRun,
    const DetectionSource &detectionSource)
    : m_id(Id::generate())
    , m_name(name)
    , m_executable(executable)
    , m_qchFile(qchFile)
    , m_detectionSource(detectionSource)
    , m_isAutoRun(autoRun)
{
    updateErrorFlags();
}

void CMakeToolTreeItem::updateErrorFlags()
{
    std::unique_ptr<CMakeTool> temporaryTool;
    CMakeTool *cmake = CMakeToolManager::findById(m_id);
    if (!cmake) {
        temporaryTool = std::make_unique<CMakeTool>(m_detectionSource, m_id);
        cmake = temporaryTool.get();
        cmake->setFilePath(m_executable);
    }
    cmake->setFilePath(m_executable);
    m_isSupported = cmake->hasFileApi();
    m_hasError = !m_isSupported || !CMakeTool::cmakeExecutable(m_executable).isExecutableFile();

    m_tooltip = Tr::tr("Version: %1").arg(cmake->versionDisplay());
    m_tooltip += "<br>"
                 + Tr::tr("Supports fileApi: %1").arg(m_isSupported ? Tr::tr("yes") : Tr::tr("no"));
    m_tooltip += "<br>" + Tr::tr("Detection source: \"%1\"").arg(m_detectionSource.id);

    m_versionDisplay = cmake->versionDisplay();

    // Make sure to always have the right version in the name for Qt SDK CMake installations
    if (m_detectionSource.isAutoDetected() && m_name.startsWith("CMake") && m_name.endsWith("(Qt)"))
        m_name = QString("CMake %1 (Qt)").arg(m_versionDisplay);
}

QVariant CMakeToolTreeItem::data(int column, int role) const
{
    switch (role) {
    case FilePathRole: return m_executable.toVariant();
    case Qt::DisplayRole: {
        switch (column) {
        case 0:
            return m_name;
        case 1:
            return m_executable.toUserOutput();
        } // switch (column)
        return QVariant();
    }
    case Qt::FontRole:
        return {};
    case Qt::ToolTipRole: {
        QString result = m_tooltip;
        QString error;
        const FilePath filePath = CMakeTool::cmakeExecutable(m_executable);
        if (!filePath.exists()) {
            error = Tr::tr("CMake executable path does not exist.");
        } else if (!filePath.isFile()) {
            error = Tr::tr("CMake executable path is not a file.");
        } else if (!filePath.isExecutableFile()) {
            error = Tr::tr("CMake executable path is not executable.");
        } else if (!m_isSupported) {
            error = Tr::tr("CMake executable does not provide required IDE integration features.");
        }
        if (result.isEmpty() || error.isEmpty())
            return QString("%1%2").arg(result).arg(error);
        else
            return QString("%1<br><br><b>%2</b>").arg(result).arg(error);
    }
    case Qt::DecorationRole: {
        if (column == 0 && m_hasError)
            return Icons::CRITICAL.icon();
        return QVariant();
    }
    case ProjectExplorer::KitAspect::IdRole:
        return m_id.toSetting();
    case ProjectExplorer::KitAspect::QualityRole:
        return m_hasError ? 0 : 1;
    }
    return QVariant();
}

CMakeToolItemModel::CMakeToolItemModel()
{
    setShowDefault(true);
    setHeader({Tr::tr("Name"), Tr::tr("Path")});
    setFilters(ProjectExplorer::Constants::msgAutoDetected(),
               {{Tr::tr("Manual"), [this](int row) {
                   return !item(row).m_detectionSource.isAutoDetected();
               }}});

    const QList<CMakeTool *> tools = CMakeToolManager::cmakeTools();
    for (const CMakeTool *tool : tools)
        addCMakeTool(tool);

    if (const CMakeTool *defTool = CMakeToolManager::defaultCMakeTool()) {
        if (const int row = rowForId(defTool->id()); row >= 0)
            setDefaultRow(row);
    }

    connect(CMakeToolManager::instance(), &CMakeToolManager::cmakeRemoved,
            this, &CMakeToolItemModel::removeCMakeTool);
    connect(CMakeToolManager::instance(), &CMakeToolManager::cmakeAdded,
            this, [this](const Id &id) { addCMakeTool(CMakeToolManager::findById(id)); });
}

void CMakeToolItemModel::addCMakeTool(const CMakeTool *item)
{
    QTC_ASSERT(item, return);

    if (rowForId(item->id()) >= 0)
        return;

    appendItem(CMakeToolTreeItem(item));
}

void CMakeToolItemModel::updateCMakeTool(const Id &id,
                                         const QString &displayName,
                                         const FilePath &executable,
                                         const FilePath &qchFile)
{
    const int row = rowForId(id);
    QTC_ASSERT(row >= 0, return);

    CMakeToolTreeItem it = item(row);
    it.m_name = displayName;
    it.m_executable = executable;
    it.m_qchFile = qchFile;
    it.updateErrorFlags();
    setVolatileItem(row, it);
    notifyRowChanged(row);
}

int CMakeToolItemModel::cloneRow(int row)
{
    const CMakeToolTreeItem it = item(row);
    const CMakeToolTreeItem clone = {
        Tr::tr("Clone of %1").arg(it.m_name),
        it.m_executable,
        it.m_qchFile,
        it.m_isAutoRun,
        DetectionSource{DetectionSource::Manual, it.m_detectionSource.id}
    };
    return appendVolatileItem(clone);
}

int CMakeToolItemModel::rowForId(Id id) const
{
    for (int row = 0; row < itemCount(); ++row) {
        if (item(row).m_id == id)
            return row;
    }
    return -1;
}

void CMakeToolItemModel::removeCMakeTool(const Id &id)
{
    // Called via the cmakeRemoved signal: the tool is already gone from the manager.
    const int row = rowForId(id);
    if (row < 0 || isRemoved(row))
        return;

    removeItem(row);
}

void CMakeToolItemModel::apply()
{
    for (int row = 0; row < itemCount(); ++row) {
        if (isRemoved(row))
            CMakeToolManager::deregisterCMakeTool(item(row).m_id);
    }

    QList<int> toRegister;
    for (int row = 0; row < itemCount(); ++row) {
        if (isRemoved(row))
            continue;
        const CMakeToolTreeItem it = item(row);
        if (CMakeTool *cmake = CMakeToolManager::findById(it.m_id)) {
            cmake->setDisplayName(it.m_name);
            cmake->setFilePath(it.m_executable);
            cmake->setQchFilePath(it.m_qchFile);
            cmake->setDetectionSource(it.m_detectionSource);
        } else {
            toRegister.append(row);
        }
    }

    QList<Id> failedIds;
    for (int row : std::as_const(toRegister)) {
        const CMakeToolTreeItem it = item(row);
        auto cmake = std::make_unique<CMakeTool>(it.m_detectionSource, it.m_id);
        cmake->setDisplayName(it.m_name);
        cmake->setFilePath(it.m_executable);
        cmake->setQchFilePath(it.m_qchFile);
        if (!CMakeToolManager::registerCMakeTool(std::move(cmake)))
            failedIds.append(it.m_id);
    }

    const int defRow = defaultRow();
    CMakeToolManager::setDefaultCMakeTool(defRow >= 0 ? item(defRow).m_id : Id());
    GroupedModel::apply();

    for (int row = 0; row < itemCount(); ++row) {
        if (failedIds.contains(item(row).m_id))
            setChanged(row, true);
    }
}

QString CMakeToolItemModel::uniqueDisplayName(const QString &base) const
{
    QStringList names;
    for (int row = 0; row < itemCount(); ++row)
        names << item(row).m_name;
    return Utils::makeUniquelyNumbered(base, names);
}

//
// CMakeToolsAspects
//

// What the Tools page edits. The tools themselves live in CMakeToolManager,
// which the rest of the plugin reads; CMakeToolItemModel is the editable copy,
// and applying the page is applying it.
class CMakeToolsAspects final : public AspectContainer
{
public:
    CMakeToolsAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CMakeProjectManager/CMakeToolsPage.qml"));

        device.setQmlName("Device");

        tools.setQmlName("Tools");
        tools.setModel(&m_model);
        tools.setShowsDefault(true);
        tools.setToolTip(Tr::tr("Set as the default CMake Tool to use when "
                                "creating a new kit or when no value is set."));
        // An auto-detected tool is not the user's to take away.
        tools.setCanRemoveRow([this](int row) {
            return !m_model.item(row).m_detectionSource.isAutoDetected();
        });

        add.setQmlName("Add");
        add.setActionText(Tr::tr("Add"));
        add.setAction([this] { addCMakeTool(); });

        redetect.setQmlName("Redetect");
        redetect.setActionText(Tr::tr("Re-detect"));
        redetect.setAction([this] { redetectTools(); });

        details.setQmlName("Details");
        displayName.setQmlName("DisplayName");
        displayName.setDisplayStyle(StringAspect::LineEditDisplay);
        displayName.setLabelText(Tr::tr("Name:"));

        binary.setQmlName("Binary");
        binary.setExpectedKind(PathChooserKind::ExistingCommand);
        binary.setHistoryCompleter("Cmake.Command.History");
        binary.setCommandVersionArguments({"--version"});
        binary.setAllowPathFromDevice(true);
        binary.setLabelText(Tr::tr("Path:"));

        version.setQmlName("Version");
        version.setDisplayStyle(StringAspect::LabelDisplay);
        version.setLabelText(Tr::tr("Version:"));

        qchFile.setQmlName("QchFile");
        qchFile.setExpectedKind(PathChooserKind::File);
        qchFile.setHistoryCompleter("Cmake.qchFile.History");
        qchFile.setPromptDialogFilter("*.qch");
        qchFile.setPromptDialogTitle(Tr::tr("CMake .qch File"));
        qchFile.setLabelText(Tr::tr("Help file:"));

        // Behaviour, not layout.
        connect(&device, &DeviceSelectionAspect::currentDeviceChanged,
                this, &CMakeToolsAspects::applyDeviceFilter);
        applyDeviceFilter();

        connect(&tools, &GroupedListAspect::currentRowChanged,
                this, [this](int, int newRow) { showTool(newRow); });
        binary.addOnVolatileValueChanged(this, [this] { onBinaryChanged(); });
        qchFile.addOnVolatileValueChanged(this, [this] { store(); });
        displayName.addOnVolatileValueChanged(this, [this] { store(); });
        showTool(-1);
    }

    void apply() override
    {
        AspectContainer::apply();
        m_model.apply();
    }

    void cancel() override
    {
        AspectContainer::cancel();
        m_model.cancel();
    }

    bool isDirty() const override
    {
        return AspectContainer::isDirty() || m_model.isDirty();
    }

    DeviceSelectionAspect device{this};
    GroupedListAspect tools{this};
    ActionAspect add{this};
    ActionAspect redetect{this};
    AspectContainer details{this};
    StringAspect displayName{&details};
    FilePathAspect binary{&details};
    StringAspect version{&details};
    FilePathAspect qchFile{&details};

private:
    // Only the tools that live on the selected device are listed; the entry
    // for all devices takes the filter away again.
    void applyDeviceFilter()
    {
        const IDeviceConstPtr dev = device.currentDevice();
        const FilePath deviceRoot = dev ? dev->rootPath() : FilePath();
        m_model.setExtraFilter(deviceRoot.isEmpty()
            ? GroupedModel::Filter{}
            : GroupedModel::Filter{[this, deviceRoot](int row) {
                  const FilePath path = m_model.item(row).m_executable;
                  return path.isEmpty() || path.isSameDevice(deviceRoot);
              }});
    }

    void showTool(int row, bool updateCMakePath = true)
    {
        const bool hasItem = row >= 0 && row < m_model.itemCount();
        details.setVisible(hasItem);
        // No tool is current while the form is being filled in, so nothing the
        // fields say on the way is written back. See store().
        m_id = Id();
        if (hasItem) {
            const CMakeToolTreeItem it = m_model.item(row);
            const bool autoDetected = it.m_detectionSource.isAutoDetected();
            displayName.setEnabled(!autoDetected);
            displayName.setValue(it.m_name);
            binary.setReadOnly(autoDetected);
            if (updateCMakePath)
                binary.setValue(it.m_executable);
            qchFile.setReadOnly(autoDetected);
            qchFile.setBaseDirectory(it.m_executable.parentDir());
            qchFile.setValue(it.m_qchFile);
            version.setValue(it.m_versionDisplay);
            m_id = it.m_id;
            if (const IDeviceConstPtr dev = device.currentDevice())
                binary.setInitialBrowsePathBackup(dev->rootPath());
        }
    }

    // Loading the form must not write it back: the fields hand over expanded
    // paths, so merely looking at a tool would replace what the user wrote.
    // showTool() clears the id for the duration, which is what stops that -
    // there is nothing to write to until it has finished.
    void store()
    {
        if (m_id.isValid()) {
            m_model.updateCMakeTool(m_id,
                                    displayName.volatileValue(),
                                    binary.expandedVolatileValue(),
                                    qchFile.expandedVolatileValue());
        }
    }

    // A new binary means a new version to show and, where none was named, the
    // help file that sits beside it. Loading a tool into the form sets this
    // field too, and reacting to that would hide the form it is loading - so
    // it waits for the same thing store() does, there being no tool to act on
    // until showTool() has finished.
    void onBinaryChanged()
    {
        if (!m_id.isValid())
            return;
        if (qchFile().isEmpty())
            qchFile.setValue(CMakeTool::searchQchFile(binary.expandedVolatileValue()));
        store();
        // Show what the tool became - the version comes from running it - and
        // leave the path the user is typing alone.
        showTool(m_model.rowForId(m_id), /*updateCMakePath=*/false);
    }

    void addCMakeTool()
    {
        const CMakeToolTreeItem added = {
            m_model.uniqueDisplayName(Tr::tr("New CMake")),
            FilePath(),
            FilePath(),
            true,
            DetectionSource::Manual
        };
        tools.setCurrentRow(m_model.appendVolatileItem(added));
    }

    void redetectTools()
    {
        // Step 1: Detect
        std::vector<std::unique_ptr<CMakeTool>> toAdd;
        for (const IDeviceConstPtr &dev : device.selectedDevices()) {
            auto detected
                = CMakeToolManager::autoDetectCMakeTools(dev->toolSearchPaths(), dev->rootPath());
            for (auto &&tool : detected)
                toAdd.push_back(std::move(tool));
        }

        // Step 2: Match existing against newly detected.
        QList<Id> toRemove;
        for (int row = 0; row < m_model.itemCount(); ++row) {
            const CMakeToolTreeItem treeItem = m_model.item(row);
            bool hasMatch = false;
            for (auto it = toAdd.begin(); it != toAdd.end(); ++it) {
                if ((*it)->filePath().isSameFile(treeItem.executable())) {
                    hasMatch = true;
                    toAdd.erase(it);
                    break;
                }
            }
            if (treeItem.detectionSource().isSystemDetected() && !hasMatch)
                toRemove << treeItem.id();
        }

        // Step 3: Remove previously auto-detected that were not newly detected.
        for (const Id &id : std::as_const(toRemove))
            m_model.removeCMakeTool(id);

        // Step 4: Add newly detected that haven't been present so far.
        for (const auto &tool : toAdd) {
            m_model.addCMakeTool(tool.get());
            if (const int row = m_model.rowForId(tool->id()); row >= 0)
                m_model.setChanged(row, true);
        }
    }

    CMakeToolItemModel m_model;
    // The tool the form is showing, and nothing while it is being filled in.
    Id m_id;
};

// CMakeSettingsPage

class CMakeSettingsPage final : public Core::IOptionsPage
{
public:
    CMakeSettingsPage()
    {
        setId(Constants::Settings::TOOLS_ID);
        setDisplayName(Tr::tr("Tools"));
        setCategory(Constants::Settings::CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<CMakeToolsAspects> theToolsAspects;
            return theToolsAspects.get();
        });
    }
};

#ifdef WITH_TESTS

// The page's tools lived in a QTreeView's selection and a details widget that
// was shown and hidden, so which tool was being edited - and whether it could
// be - could only be read back out of widgets.

class CMakeToolsSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testNoToolIsShownUntilOneIsPicked();
    void testEditingAFieldUpdatesTheTool();
    void testShowingAToolDoesNotRewriteIt();
    void testAnAutoDetectedToolIsShownButNotEditable();
    void testTheDeviceSelectorOffersAllDevicesFirst();
    void testNarrowingToADeviceLeavesOtherDevicesToolsOut();

private:
    static CMakeToolItemModel *modelOf(const CMakeToolsAspects &page)
    {
        return static_cast<CMakeToolItemModel *>(page.tools.model());
    }

    // How many tools the tree actually shows, which is what the device
    // selector narrows - the model keeps every one of them.
    static int shownRows(const CMakeToolsAspects &page)
    {
        QAbstractItemModel *tree = page.tools.displayModel();
        int rows = 0;
        for (int group = 0; group < tree->rowCount({}); ++group)
            rows += tree->rowCount(tree->index(group, 0));
        return rows;
    }

    static int rowThatIsAutoDetected(const CMakeToolsAspects &page)
    {
        CMakeToolItemModel *model = modelOf(page);
        for (int row = 0; row < model->itemCount(); ++row) {
            if (model->item(row).m_detectionSource.isAutoDetected())
                return row;
        }
        return -1;
    }
};

void CMakeToolsSettingsTest::testNoToolIsShownUntilOneIsPicked()
{
    CMakeToolsAspects page;
    QVERIFY(!page.details.isVisible());
    QCOMPARE(page.tools.currentRow(), -1);
    QVERIFY(!page.tools.canClone());
    QVERIFY(!page.tools.canRemove());

    page.add.triggerAction();
    QVERIFY(page.tools.currentRow() >= 0);
    QVERIFY(page.details.isVisible());
    QVERIFY(page.displayName.volatileValue().startsWith("New CMake"));
    // A tool the user added is theirs to edit and to take away again.
    QVERIFY(page.displayName.isEnabled());
    QVERIFY(page.tools.canRemove());
}

void CMakeToolsSettingsTest::testEditingAFieldUpdatesTheTool()
{
    CMakeToolsAspects page;
    page.add.triggerAction();
    const int row = page.tools.currentRow();
    QVERIFY(row >= 0);
    CMakeToolItemModel *model = modelOf(page);

    // Each field on its own: they store through the same call, so setting both
    // before looking would let either one carry the other.
    page.displayName.setVolatileValue(QString("Renamed"));
    QCOMPARE(model->item(row).m_name, QString("Renamed"));
    page.qchFile.setVolatileValue(QString("/help/cmake.qch"));
    QCOMPARE(model->item(row).m_qchFile, FilePath::fromString("/help/cmake.qch"));

    // Showing another tool must not write the one that was on screen into it.
    page.add.triggerAction();
    const int second = page.tools.currentRow();
    QVERIFY(second != row);
    page.displayName.setVolatileValue(QString("Second"));
    QCOMPARE(model->item(second).m_name, QString("Second"));
    QCOMPARE(model->item(row).m_name, QString("Renamed"));
    QCOMPARE(model->item(row).m_qchFile, FilePath::fromString("/help/cmake.qch"));

    page.tools.setCurrentRow(row);
    QCOMPARE(page.displayName.volatileValue(), QString("Renamed"));
    QCOMPARE(page.qchFile.volatileValue(), QString("/help/cmake.qch"));
}

void CMakeToolsSettingsTest::testShowingAToolDoesNotRewriteIt()
{
    CMakeToolsAspects page;
    page.add.triggerAction();
    const int row = page.tools.currentRow();
    QVERIFY(row >= 0);
    CMakeToolItemModel *model = modelOf(page);

    // A path written with a variable in it stays as it was written. The fields
    // hand back expanded paths, so loading a tool into them must not store
    // what they just read.
    const QString written = "/help/%{HostOs:PathListSeparator}/cmake.qch";
    model->updateCMakeTool(model->item(row).m_id, "With a variable", {},
                           FilePath::fromString(written));
    QVERIFY(page.qchFile.expandedVolatileValue() != FilePath::fromString(written));

    page.tools.setCurrentRow(-1);
    page.tools.setCurrentRow(row);

    QCOMPARE(model->item(row).m_qchFile, FilePath::fromString(written));
}

void CMakeToolsSettingsTest::testAnAutoDetectedToolIsShownButNotEditable()
{
    CMakeToolsAspects page;
    const int row = rowThatIsAutoDetected(page);
    if (row < 0)
        QSKIP("No CMake was found on this machine, so there is no auto-detected tool.");

    page.tools.setCurrentRow(row);
    QVERIFY(page.details.isVisible());
    QVERIFY(!page.displayName.isEnabled());
    QVERIFY(page.binary.isReadOnly());
    QVERIFY(page.qchFile.isReadOnly());
    QVERIFY(!page.tools.canRemove());
    // It is still worth copying, and the copy is the user's.
    QVERIFY(page.tools.canClone());
}

void CMakeToolsSettingsTest::testTheDeviceSelectorOffersAllDevicesFirst()
{
    CMakeToolsAspects page;

    // The entry for all of them comes first and is what a page starts on, so
    // nothing is hidden until the user asks for it.
    QVERIFY(page.device.optionCount() >= 1);
    QCOMPARE(page.device.value(), 0);
    QVERIFY(!page.device.itemValue().isValid());
    QVERIFY(!page.device.currentDevice());
    QCOMPARE(page.device.selectedDevices().size(), DeviceManager::deviceCount());

    if (DeviceManager::deviceCount() == 0)
        QSKIP("No devices are configured, so there is nothing to select.");
    page.device.setValue(1);
    QVERIFY(page.device.currentDevice());
    QCOMPARE(page.device.selectedDevices().size(), 1);
    QCOMPARE(page.device.selectedDevices().first()->id(), page.device.currentDevice()->id());

    // The options are rebuilt whenever the device list changes, and what is
    // selected is remembered by id: a position would put the page back on the
    // entry for all devices, or on somebody else's device.
    const Id chosen = page.device.currentDevice()->id();
    QMetaObject::invokeMethod(DeviceManager::instance(), "deviceUpdated", Q_ARG(Utils::Id, chosen));
    QVERIFY(page.device.currentDevice());
    QCOMPARE(page.device.currentDevice()->id(), chosen);
}

void CMakeToolsSettingsTest::testNarrowingToADeviceLeavesOtherDevicesToolsOut()
{
    const IDeviceConstPtr desktop = DeviceManager::defaultDesktopDevice();
    if (!desktop)
        QSKIP("No desktop device is configured, so there is nothing to narrow to.");

    CMakeToolsAspects page;
    CMakeToolItemModel *model = modelOf(page);
    page.add.triggerAction();
    const int row = page.tools.currentRow();
    // A path on some other machine, which is what the selector is for.
    page.binary.setVolatileValue(QString("docker://abc/usr/bin/cmake"));
    QCOMPARE(model->item(row).m_executable.host(), QString("abc"));

    const int items = model->itemCount();
    const int withAll = shownRows(page);
    QVERIFY(withAll > 0);

    page.device.setValue(page.device.indexForItemValue(desktop->id().toSetting()));
    QCOMPARE(page.device.currentDevice()->id(), desktop->id());

    // Out of the list, not out of the model: narrowing is a view of what is
    // there, and applying the page must not lose the tool.
    QCOMPARE(shownRows(page), withAll - 1);
    QCOMPARE(model->itemCount(), items);

    // And back again.
    page.device.setValue(0);
    QCOMPARE(shownRows(page), withAll);
}

QObject *createCMakeToolsSettingsTest()
{
    return new CMakeToolsSettingsTest;
}

#endif // WITH_TESTS

void setupCMakeSettingsPage()
{
    static CMakeSettingsPage theCMakeSettingsPage;
}

} // CMakeProjectManager::Internal

#include "cmakesettingspage.moc"

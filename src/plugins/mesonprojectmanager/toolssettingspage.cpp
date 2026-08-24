// Copyright (C) 2020 Alexis Jeandet.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "toolssettingspage.h"

#include "mesonpluginconstants.h"
#include "mesonprojectmanagertr.h"
#include "mesontools.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <projectexplorer/projectexplorerconstants.h>

#include <utils/aspects.h>
#include <utils/groupedlistaspect.h>
#include <utils/groupedmodel.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/shutdownguard.h>
#include <utils/stringutils.h>
#include <utils/utilsicons.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace MesonProjectManager::Internal {

class ToolItem
{
public:
    ToolItem() = default;
    explicit ToolItem(const QString &name);
    explicit ToolItem(const MesonTools::Tool_t &tool);
    ToolItem cloned() const;

    QVariant data(int column, int role) const;

    friend bool operator==(const ToolItem &, const ToolItem &) = default;

    QString name;
    FilePath executable;
    Id id;
    bool autoDetected = false;
};

} // namespace MesonProjectManager::Internal

Q_DECLARE_METATYPE(MesonProjectManager::Internal::ToolItem)

namespace MesonProjectManager::Internal {

ToolItem::ToolItem(const QString &name)
    : name{name}
    , id{Id::generate()}
    , autoDetected{false}
{}

ToolItem::ToolItem(const MesonTools::Tool_t &tool)
    : name{tool->name()}
    , executable{tool->exe()}
    , id{tool->id()}
    , autoDetected{tool->autoDetected()}
{}

ToolItem ToolItem::cloned() const
{
    ToolItem result;
    result.name = Tr::tr("Clone of %1").arg(name);
    result.executable = executable;
    result.id = Id::generate();
    result.autoDetected = false;
    return result;
}

QVariant ToolItem::data(int column, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        switch (column) {
        case 0:
            return name;
        case 1:
            return executable.toUserOutput();
        }
        return {};
    case Qt::ToolTipRole: {
        if (!executable.exists())
            return Tr::tr("Meson executable path does not exist.");
        if (!executable.isFile())
            return Tr::tr("Meson executable path is not a file.");
        if (!executable.isExecutableFile())
            return Tr::tr("Meson executable path is not executable.");
        const QVersionNumber ver = MesonToolWrapper::read_version(executable);
        return ver.isNull() ? Tr::tr("Cannot get tool version.")
                            : Tr::tr("Version: %1").arg(ver.toString());
    }
    case Qt::DecorationRole:
        if (column == 0 && !executable.isExecutableFile())
            return Icons::CRITICAL.icon();
        return {};
    }
    return {};
}

// ToolsModel

class ToolsModel final : public TypedGroupedModel<ToolItem>
{
public:
    ToolsModel();

    int addMesonTool();
    int cloneRow(int row) override;
    void updateItem(int row, const QString &name, const FilePath &exe);
    void apply() override;

private:
    QVariant variantData(int row, int column, int role) const override;
    QString uniqueName(const QString &baseName) const;
};

ToolsModel::ToolsModel()
{
    setShowDefault(true);
    setHeader({Tr::tr("Name"), Tr::tr("Location")});
    setFilters(ProjectExplorer::Constants::msgAutoDetected(),
               {{ProjectExplorer::Constants::msgManual(), [this](int row) {
                    return !item(row).autoDetected;
                }}});
    for (const MesonTools::Tool_t &tool : MesonTools::tools())
        appendItem(ToolItem{tool});
    const Id defaultId = MesonTools::defaultToolId();
    for (int row = 0; row < itemCount(); ++row) {
        if (item(row).id == defaultId) {
            setDefaultRow(row);
            break;
        }
    }
}

int ToolsModel::addMesonTool()
{
    return appendVolatileItem(ToolItem{uniqueName(Tr::tr("New Meson"))});
}

int ToolsModel::cloneRow(int row)
{
    return appendVolatileItem(item(row).cloned());
}

void ToolsModel::updateItem(int row, const QString &name, const FilePath &exe)
{
    QTC_ASSERT(row >= 0, return);
    ToolItem it = item(row);
    it.name = name;
    it.executable = exe;
    setVolatileItem(row, it);
    notifyRowChanged(row);
}

void ToolsModel::apply()
{
    const int defRow = defaultRow();
    MesonTools::setDefaultToolId(defRow >= 0 ? item(defRow).id : Id());
    for (int row = 0; row < itemCount(); ++row) {
        if (isRemoved(row)) {
            MesonTools::removeTool(item(row).id);
            continue;
        }
        if (isDirty(row)) {
            const ToolItem it = item(row);
            MesonTools::updateTool(it.id, it.name, it.executable);
        }
    }

    GroupedModel::apply();
}

QVariant ToolsModel::variantData(int row, int column, int role) const
{
    return item(row).data(column, role);
}

QString ToolsModel::uniqueName(const QString &baseName) const
{
    QStringList names;
    for (int row = 0; row < itemCount(); ++row)
        names << item(row).name;
    return Utils::makeUniquelyNumbered(baseName, names);
}

// ToolsSettingsAspects

// What the Tools page edits. The tools themselves live in MesonTools, which
// the rest of the plugin reads; ToolsModel is the editable copy, and applying
// the page is applying it.
class ToolsSettingsAspects final : public AspectContainer
{
public:
    ToolsSettingsAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/MesonProjectManager/MesonToolsPage.qml"));

        tools.setQmlName("Tools");
        tools.setModel(&m_model);
        tools.setShowsDefault(true);
        tools.setToolTip(Tr::tr("Set as the default Meson executable to use "
                                "when creating a new kit or when no value is set."));
        // An auto-detected tool is not the user's to take away.
        tools.setCanRemoveRow([this](int row) { return !m_model.item(row).autoDetected; });

        add.setQmlName("Add");
        add.setActionText(Tr::tr("Add"));
        add.setAction([this] { tools.setCurrentRow(m_model.addMesonTool()); });

        details.setQmlName("Details");
        name.setQmlName("Name");
        name.setDisplayStyle(StringAspect::LineEditDisplay);
        name.setLabelText(Tr::tr("Name:"));
        executable.setQmlName("Executable");
        executable.setExpectedKind(PathChooserKind::ExistingCommand);
        executable.setHistoryCompleter("Meson.Command.History");
        executable.setLabelText(Tr::tr("Path:"));

        // Behaviour, not layout: which tool the form shows, and whether it may
        // be edited at all.
        connect(&tools, &GroupedListAspect::currentRowChanged,
                this, [this](int, int newRow) { showTool(newRow); });
        name.addOnVolatileValueChanged(this, [this] { store(); });
        executable.addOnVolatileValueChanged(this, [this] { store(); });
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

    GroupedListAspect tools{this};
    ActionAspect add{this};
    AspectContainer details{this};
    StringAspect name{&details};
    FilePathAspect executable{&details};

private:
    void showTool(int row)
    {
        const bool hasItem = row >= 0 && !m_model.isRemoved(row);
        details.setVisible(hasItem);
        if (!hasItem)
            return;
        const ToolItem it = m_model.item(row);
        m_loading = true;
        name.setEnabled(!it.autoDetected);
        name.setValue(it.name);
        executable.setEnabled(!it.autoDetected);
        executable.setValue(it.executable);
        m_loading = false;
    }

    void store()
    {
        if (m_loading)
            return;
        const int row = tools.currentRow();
        if (row >= 0 && !m_model.isRemoved(row))
            m_model.updateItem(row, name.volatileValue(), executable.expandedVolatileValue());
    }

    ToolsModel m_model;
    bool m_loading = false;
};

class ToolsSettingsPage final : public Core::IOptionsPage
{
public:
    ToolsSettingsPage()
    {
        setId(Constants::SettingsPage::TOOLS_ID);
        setDisplayName(Tr::tr("Tools"));
        setCategory(Constants::SettingsPage::CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<ToolsSettingsAspects> theToolsAspects;
            return theToolsAspects.get();
        });
    }
};

#ifdef WITH_TESTS

// The page's tools lived in a QTreeView's selection and a details widget, so
// which one was being edited - and whether it could be - could only be read
// back out of widgets.

class ToolsSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testNoToolIsShownUntilOneIsPicked();
    void testAnAutoDetectedToolIsShownButNotEditable();
    void testEditingATextFieldUpdatesTheTool();
    void testAnAutoDetectedToolCannotBeRemoved();
    void testTheFormEmptiesWhenTheToolGoes();
    void testShowingAToolDoesNotRewriteIt();

private:
    // The auto-detected tools are whatever this machine has, so a test picks
    // its rows by what they are rather than by position.
    static int rowWith(const ToolsSettingsAspects &page, bool autoDetected)
    {
        auto model = static_cast<ToolsModel *>(page.tools.model());
        for (int row = 0; row < model->itemCount(); ++row) {
            if (model->item(row).autoDetected == autoDetected)
                return row;
        }
        return -1;
    }
};

void ToolsSettingsTest::testNoToolIsShownUntilOneIsPicked()
{
    ToolsSettingsAspects page;
    QVERIFY(!page.details.isVisible());
    QCOMPARE(page.tools.currentRow(), -1);
    QVERIFY(!page.tools.canClone());
    QVERIFY(!page.tools.canRemove());

    page.add.triggerAction();
    // Adding selects what was added, and the form comes out to show it.
    QVERIFY(page.tools.currentRow() >= 0);
    QVERIFY(page.details.isVisible());
    QVERIFY(page.name.volatileValue().startsWith("New Meson"));
}

void ToolsSettingsTest::testAnAutoDetectedToolIsShownButNotEditable()
{
    ToolsSettingsAspects page;
    const int row = rowWith(page, /*autoDetected=*/true);
    if (row < 0)
        QSKIP("No Meson was found on this machine, so there is no auto-detected tool.");

    page.tools.setCurrentRow(row);
    QVERIFY(page.details.isVisible());
    QVERIFY(!page.name.isEnabled());
    QVERIFY(!page.executable.isEnabled());
    // It is still worth copying, and the copy is the user's.
    QVERIFY(page.tools.canClone());
}

void ToolsSettingsTest::testEditingATextFieldUpdatesTheTool()
{
    ToolsSettingsAspects page;
    page.add.triggerAction();
    const int row = page.tools.currentRow();
    QVERIFY(row >= 0);
    QVERIFY(page.name.isEnabled());

    auto model = static_cast<ToolsModel *>(page.tools.model());

    // Each field on its own: they store through the same call, so setting both
    // before looking would let either one carry the other.
    page.name.setVolatileValue(QString("Renamed"));
    QCOMPARE(model->item(row).name, QString("Renamed"));
    page.executable.setVolatileValue(QString("/first/meson"));
    QCOMPARE(model->item(row).executable, FilePath::fromString("/first/meson"));

    // Showing another tool must not write the one that was on screen into it.
    // Loading the form field by field looks exactly like the user typing, and
    // a half-loaded form is one tool's name beside another's path.
    page.add.triggerAction();
    const int second = page.tools.currentRow();
    QVERIFY(second != row);
    page.name.setVolatileValue(QString("Second"));
    QCOMPARE(model->item(second).name, QString("Second"));
    page.executable.setVolatileValue(QString("/second/meson"));
    QCOMPARE(model->item(second).executable, FilePath::fromString("/second/meson"));
    QCOMPARE(model->item(row).executable, FilePath::fromString("/first/meson"));

    // And coming back shows what was left there, not what was on screen last.
    page.tools.setCurrentRow(row);
    QCOMPARE(page.name.volatileValue(), QString("Renamed"));
    QCOMPARE(page.executable.volatileValue(), QString("/first/meson"));
    QCOMPARE(model->item(row).name, QString("Renamed"));
    QCOMPARE(model->item(row).executable, FilePath::fromString("/first/meson"));
    QCOMPARE(model->item(second).name, QString("Second"));
    QCOMPARE(model->item(second).executable, FilePath::fromString("/second/meson"));
}

void ToolsSettingsTest::testAnAutoDetectedToolCannotBeRemoved()
{
    ToolsSettingsAspects page;
    const int row = rowWith(page, /*autoDetected=*/true);
    if (row < 0)
        QSKIP("No Meson was found on this machine, so there is no auto-detected tool.");

    page.tools.setCurrentRow(row);
    QVERIFY(!page.tools.canRemove());

    page.add.triggerAction();
    QVERIFY(page.tools.canRemove());
}

void ToolsSettingsTest::testTheFormEmptiesWhenTheToolGoes()
{
    ToolsSettingsAspects page;
    page.add.triggerAction();
    const int row = page.tools.currentRow();
    QVERIFY(page.details.isVisible());

    page.tools.setCurrentRow(row);
    page.tools.removeCurrent();

    // Either the selection moved to a tool that is staying, or there is none
    // left; a tool on its way out is never shown.
    const int now = page.tools.currentRow();
    auto model = static_cast<ToolsModel *>(page.tools.model());
    QVERIFY(now < 0 || !model->isRemoved(now));
    QCOMPARE(page.details.isVisible(), now >= 0);
}

void ToolsSettingsTest::testShowingAToolDoesNotRewriteIt()
{
    ToolsSettingsAspects page;
    page.add.triggerAction();
    const int row = page.tools.currentRow();
    QVERIFY(row >= 0);
    auto model = static_cast<ToolsModel *>(page.tools.model());

    // A path written with a variable in it stays as it was written. The form
    // hands back the expanded path, so loading a tool into it must not store
    // what it just read - merely looking at a tool would rewrite it.
    const QString written = "/tools/%{HostOs:PathListSeparator}/meson";
    model->updateItem(row, "With a variable", FilePath::fromString(written));
    QVERIFY(page.executable.expandedVolatileValue() != FilePath::fromString(written));

    page.tools.setCurrentRow(-1);
    page.tools.setCurrentRow(row);

    QCOMPARE(model->item(row).executable, FilePath::fromString(written));
}

QObject *createToolsSettingsTest()
{
    return new ToolsSettingsTest;
}

#endif // WITH_TESTS

void setupToolsSettingsPage()
{
    static ToolsSettingsPage theToolsSettingsPage;
}

} // namespace MesonProjectManager

#include "toolssettingspage.moc"

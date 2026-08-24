// Copyright (C) 2024 The Qt Company Ltd.
// Copyright (C) 2026 BogDan Vatra <bogdan@kde.org>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gntoolssettingspage.h"

#include "gnpluginconstants.h"
#include "gnprojectmanagertr.h"
#include "gntools.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/algorithm.h>

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
using namespace ProjectExplorer;

namespace GNProjectManager::Internal {

class GNToolItem final
{
public:
    GNToolItem() = default;
    explicit GNToolItem(const QString &name);
    explicit GNToolItem(const GNTools::Tool &tool);
    GNToolItem cloned() const;

    QVariant data(int column, int role) const;
    friend bool operator==(const GNToolItem &, const GNToolItem &) = default;

    QString name;
    FilePath executable;
    Id id;
    bool autoDetected = false;
};

} // namespace GNProjectManager::Internal

Q_DECLARE_METATYPE(GNProjectManager::Internal::GNToolItem)

namespace GNProjectManager::Internal {

GNToolItem::GNToolItem(const QString &name)
    : name{name}
    , id{Id::generate()}
    , autoDetected{false}
{}

GNToolItem::GNToolItem(const GNTools::Tool &tool)
    : name{tool->name()}
    , executable{tool->exe()}
    , id{tool->id()}
    , autoDetected{tool->autoDetected()}
{}

GNToolItem GNToolItem::cloned() const
{
    GNToolItem result;
    result.name = Tr::tr("Clone of %1").arg(name);
    result.executable = executable;
    result.id = Id::generate();
    result.autoDetected = false;
    return result;
}

QVariant GNToolItem::data(int column, int role) const
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
            return Tr::tr("GN executable path does not exist.");
        if (!executable.isFile())
            return Tr::tr("GN executable path is not a file.");
        if (!executable.isExecutableFile())
            return Tr::tr("GN executable path is not executable.");
        const QVersionNumber ver = GNTool::readVersion(executable);
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


// GNToolsModel

class GNToolsModel final : public TypedGroupedModel<GNToolItem>
{
public:
    GNToolsModel();

    int addGNTool();
    int cloneRow(int row) override;
    void updateItem(int row, const QString &name, const FilePath &exe);
    void apply() override;

private:
    QVariant variantData(int row, int column, int role) const override;
    QString uniqueName(const QString &baseName) const;
};

GNToolsModel::GNToolsModel()
{
    setShowDefault(true);
    setHeader({Tr::tr("Name"), Tr::tr("Location")});
    setFilters(ProjectExplorer::Constants::msgAutoDetected(),
               {{ProjectExplorer::Constants::msgManual(), [this](int row) {
                    return !item(row).autoDetected;
                }}});
    for (const GNTools::Tool &tool : GNTools::tools())
        appendItem(GNToolItem{tool});
    const Id defaultId = GNTools::defaultToolId();
    for (int row = 0; row < itemCount(); ++row) {
        if (item(row).id == defaultId) {
            setDefaultRow(row);
            break;
        }
    }
}

int GNToolsModel::addGNTool()
{
    return appendVolatileItem(GNToolItem{uniqueName(Tr::tr("New GN"))});
}

int GNToolsModel::cloneRow(int row)
{
    return appendVolatileItem(item(row).cloned());
}

void GNToolsModel::updateItem(int row, const QString &name, const FilePath &exe)
{
    QTC_ASSERT(row >= 0, return);
    GNToolItem it = item(row);
    it.name = name;
    it.executable = exe;
    setVolatileItem(row, it);
    notifyRowChanged(row);
}

void GNToolsModel::apply()
{
    const int defRow = defaultRow();
    GNTools::setDefaultToolId(defRow >= 0 ? item(defRow).id : Id());
    for (int row = 0; row < itemCount(); ++row) {
        if (isRemoved(row)) {
            GNTools::removeTool(item(row).id);
            continue;
        }
        if (isDirty(row)) {
            const GNToolItem it = item(row);
            GNTools::updateTool(it.id, it.name, it.executable);
        }
    }

    GroupedModel::apply();
}

QVariant GNToolsModel::variantData(int row, int column, int role) const
{
    return item(row).data(column, role);
}

QString GNToolsModel::uniqueName(const QString &baseName) const
{
    QStringList names;
    for (int row = 0; row < itemCount(); ++row)
        names << item(row).name;
    return Utils::makeUniquelyNumbered(baseName, names);
}

// GNToolsSettingsWidget

// What the Tools page edits. The tools themselves live in GNTools, which the
// rest of the plugin reads; GNToolsModel is the editable copy, and applying the
// page is applying it.
class GNToolsSettingsAspects final : public AspectContainer
{
public:
    GNToolsSettingsAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/GNProjectManager/GNToolsPage.qml"));

        tools.setQmlName("Tools");
        tools.setModel(&m_model);
        tools.setShowsDefault(true);
        tools.setToolTip(Tr::tr("Set as the default GN executable to use "
                                "when creating a new kit or when no value is set."));
        // An auto-detected tool is not the user's to take away.
        tools.setCanRemoveRow([this](int row) { return !m_model.item(row).autoDetected; });

        add.setQmlName("Add");
        add.setActionText(Tr::tr("Add"));
        add.setAction([this] { tools.setCurrentRow(m_model.addGNTool()); });

        details.setQmlName("Details");
        name.setQmlName("Name");
        name.setDisplayStyle(StringAspect::LineEditDisplay);
        name.setLabelText(Tr::tr("Name:"));
        executable.setQmlName("Executable");
        executable.setExpectedKind(PathChooserKind::ExistingCommand);
        executable.setHistoryCompleter("GN.Command.History");
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
        const GNToolItem it = m_model.item(row);
        m_loading = true;
        name.setEnabled(!it.autoDetected);
        name.setValue(it.name);
        executable.setEnabled(!it.autoDetected);
        executable.setValue(it.executable);
        m_loading = false;
    }

    // Loading the form must not write it back: the field hands over the
    // expanded path, so merely looking at a tool would replace a path the user
    // wrote with a variable in it.
    void store()
    {
        if (m_loading)
            return;
        const int row = tools.currentRow();
        if (row >= 0 && !m_model.isRemoved(row))
            m_model.updateItem(row, name.volatileValue(), executable.expandedVolatileValue());
    }

    GNToolsModel m_model;
    bool m_loading = false;
};

// Setup

class GNToolsSettingsPage final : public Core::IOptionsPage
{
public:
    GNToolsSettingsPage()
    {
        setId(Constants::SettingsPage::TOOLS_ID);
        setDisplayName(Tr::tr("Tools"));
        setCategory(Constants::SettingsPage::CATEGORY);
        setSettingsProvider([] {
            static GuardedObject<GNToolsSettingsAspects> theToolsAspects;
            return theToolsAspects.get();
        });
    }
};

#ifdef WITH_TESTS

// The page's tools lived in a QTreeView's selection and a details widget, so
// which one was being edited - and whether it could be - could only be read
// back out of widgets.

class GNToolsSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testThePageRendersWithQuick();
    void testNoToolIsShownUntilOneIsPicked();
    void testEditingATextFieldUpdatesTheTool();
    void testShowingAToolDoesNotRewriteIt();
    void testAnAutoDetectedToolIsNotEditableOrRemovable();
    void testTheFormEmptiesWhenTheToolGoes();

private:
    // The auto-detected tools are whatever this machine has, so a test picks
    // its rows by what they are rather than by position.
    static int rowWith(const GNToolsSettingsAspects &page, bool autoDetected)
    {
        auto model = static_cast<GNToolsModel *>(page.tools.model());
        for (int row = 0; row < model->itemCount(); ++row) {
            if (model->item(row).autoDetected == autoDetected)
                return row;
        }
        return -1;
    }
};

// This plugin is DisabledByDefault, so its page is not in
// Core::IOptionsPage::allOptionsPages() during the QuickUi run that checks
// every page renders - nothing else would ever load this file. See the note in
// the migration plan.
void GNToolsSettingsTest::testThePageRendersWithQuick()
{
    Core::IOptionsPage *page = Utils::findOrDefault(
        Core::IOptionsPage::allOptionsPages(), [](Core::IOptionsPage *page) {
            return page->id() == Constants::SettingsPage::TOOLS_ID;
        });
    QVERIFY(page);
    const std::unique_ptr<QWidget> widget(page->createWidget());
    QVERIFY(widget);

    // Named rather than typed: this plugin does not link QuickWidgets, and
    // adding a dependency to look at one would be the wrong way round.
    QObject *quick = Utils::findOrDefault(
        widget->findChildren<QObject *>(), [](QObject *child) {
            return qstrcmp(child->metaObject()->className(), "QQuickWidget") == 0;
        });
    QVERIFY2(quick, "the page did not render with Qt Quick");
    // QQuickWidget::Ready. An error leaves it at Error with no root object,
    // which is a page showing nothing at all.
    QCOMPARE(quick->property("status").toInt(), 1);
}

void GNToolsSettingsTest::testNoToolIsShownUntilOneIsPicked()
{
    GNToolsSettingsAspects page;
    QVERIFY(!page.details.isVisible());
    QCOMPARE(page.tools.currentRow(), -1);
    QVERIFY(!page.tools.canClone());
    QVERIFY(!page.tools.canRemove());

    page.add.triggerAction();
    QVERIFY(page.tools.currentRow() >= 0);
    QVERIFY(page.details.isVisible());
    QVERIFY(page.name.volatileValue().startsWith("New GN"));
}

void GNToolsSettingsTest::testEditingATextFieldUpdatesTheTool()
{
    GNToolsSettingsAspects page;
    page.add.triggerAction();
    const int row = page.tools.currentRow();
    QVERIFY(row >= 0);
    auto model = static_cast<GNToolsModel *>(page.tools.model());

    // Each field on its own: they store through the same call, so setting both
    // before looking would let either one carry the other.
    page.name.setVolatileValue(QString("Renamed"));
    QCOMPARE(model->item(row).name, QString("Renamed"));
    page.executable.setVolatileValue(QString("/first/gn"));
    QCOMPARE(model->item(row).executable, FilePath::fromString("/first/gn"));

    // Showing another tool must not write the one that was on screen into it.
    page.add.triggerAction();
    const int second = page.tools.currentRow();
    QVERIFY(second != row);
    page.name.setVolatileValue(QString("Second"));
    QCOMPARE(model->item(second).name, QString("Second"));
    QCOMPARE(model->item(row).executable, FilePath::fromString("/first/gn"));

    page.tools.setCurrentRow(row);
    QCOMPARE(page.name.volatileValue(), QString("Renamed"));
    QCOMPARE(page.executable.volatileValue(), QString("/first/gn"));
}

void GNToolsSettingsTest::testShowingAToolDoesNotRewriteIt()
{
    GNToolsSettingsAspects page;
    page.add.triggerAction();
    const int row = page.tools.currentRow();
    QVERIFY(row >= 0);
    auto model = static_cast<GNToolsModel *>(page.tools.model());

    // A path written with a variable in it stays as it was written. The form
    // hands back the expanded path, so loading a tool into it must not store
    // what it just read.
    const QString written = "/tools/%{HostOs:PathListSeparator}/gn";
    model->updateItem(row, "With a variable", FilePath::fromString(written));
    QVERIFY(page.executable.expandedVolatileValue() != FilePath::fromString(written));

    page.tools.setCurrentRow(-1);
    page.tools.setCurrentRow(row);

    QCOMPARE(model->item(row).executable, FilePath::fromString(written));
}

void GNToolsSettingsTest::testAnAutoDetectedToolIsNotEditableOrRemovable()
{
    GNToolsSettingsAspects page;
    const int row = rowWith(page, /*autoDetected=*/true);
    if (row < 0)
        QSKIP("No GN was found on this machine, so there is no auto-detected tool.");

    page.tools.setCurrentRow(row);
    QVERIFY(page.details.isVisible());
    QVERIFY(!page.name.isEnabled());
    QVERIFY(!page.executable.isEnabled());
    QVERIFY(!page.tools.canRemove());
    // It is still worth copying, and the copy is the user's.
    QVERIFY(page.tools.canClone());

    page.add.triggerAction();
    QVERIFY(page.tools.canRemove());
}

void GNToolsSettingsTest::testTheFormEmptiesWhenTheToolGoes()
{
    GNToolsSettingsAspects page;
    page.add.triggerAction();
    const int row = page.tools.currentRow();
    QVERIFY(page.details.isVisible());

    page.tools.setCurrentRow(row);
    page.tools.removeCurrent();

    // Either the selection moved to a tool that is staying, or there is none
    // left; a tool on its way out is never shown.
    const int now = page.tools.currentRow();
    auto model = static_cast<GNToolsModel *>(page.tools.model());
    QVERIFY(now < 0 || !model->isRemoved(now));
    QCOMPARE(page.details.isVisible(), now >= 0);
}

QObject *createGNToolsSettingsTest()
{
    return new GNToolsSettingsTest;
}

#endif // WITH_TESTS

void setupGNToolsSettingsPage()
{
    static GNToolsSettingsPage theGNToolsSettingsPage;
}

} // namespace GNProjectManager::Internal

#include "gntoolssettingspage.moc"

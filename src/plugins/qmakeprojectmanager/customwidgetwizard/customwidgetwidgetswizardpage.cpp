// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "customwidgetwidgetswizardpage.h"
#include "../qmakeprojectmanagertr.h"

#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/aspectwidgets.h>
#include <utils/pathchooser.h>
#include <utils/wizard.h>

#include <QFileInfo>
#include <QRegularExpression>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <coreplugin/dialogs/ioptionspage.h>
#include <QTest>
#endif

using namespace Utils;

namespace QmakeProjectManager::Internal {

QString widgetProjectFileName(const QString &library, bool linkLibrary)
{
    return QFileInfo(library).completeBaseName()
           + (linkLibrary ? QLatin1String(".pro") : QLatin1String(".pri"));
}

QString xmlFromClassName(const QString &name)
{
    QString rc = QLatin1String("<widget class=\"");
    rc += name;
    rc += QLatin1String("\" name=\"");
    if (!name.isEmpty()) {
        rc += name.left(1).toLower();
        if (name.size() > 1)
            rc += name.mid(1);
    }
    rc += QLatin1String("\">\n</widget>\n");
    return rc;
}

// One custom widget: what it is called, where its sources go, what Designer
// says about it, and the XML it starts from. The widget form put these on
// three tabs; nested containers are what the aspect vocabulary has, so they
// are three groups.
class WidgetClassAspects final : public AspectContainer
{
public:
    explicit WidgetClassAspects(const FileNamingParameters &naming);

    PluginOptions::WidgetOptions widgetOptions() const;

    StringAspect className{this};

    AspectContainer sources{this};
    SelectionAspect sourceType{&sources};
    BoolAspect createSkeleton{&sources};
    StringAspect widgetLibrary{&sources};
    StringAspect widgetProject{&sources};
    StringAspect widgetHeader{&sources};
    StringAspect widgetSource{&sources};
    StringAspect widgetBaseClass{&sources};
    StringAspect pluginClass{&sources};
    StringAspect pluginHeader{&sources};
    StringAspect pluginSource{&sources};
    FilePathAspect iconFile{&sources};

    AspectContainer description{this};
    StringAspect group{&description};
    StringAspect toolTip{&description};
    StringAspect whatsThis{&description};
    BoolAspect isContainer{&description};

    AspectContainer propertyDefaults{this};
    StringAspect domXml{&propertyDefaults};

private:
    void showWhatCanBeSaidAboutTheSources();

    const FileNamingParameters m_naming;
    // The XML follows the class name until someone writes their own.
    bool m_domXmlIsTheirs = false;
};

WidgetClassAspects::WidgetClassAspects(const FileNamingParameters &naming)
    : m_naming(naming)
{
    setAutoApply(true);

    const auto field = [](StringAspect &aspect, const char *qmlName, const QString &label) {
        aspect.setQmlName(QString::fromLatin1(qmlName));
        aspect.setLabelText(label);
        aspect.setDisplayStyle(StringAspect::LineEditDisplay);
    };

    className.setQmlName("ClassName");
    className.setLabelText(Tr::tr("Class name:"));
    className.setDisplayStyle(StringAspect::LineEditDisplay);
    // What the widget list's own model refused: a C++ identifier.
    className.setValidationFunction([](const QString &name) -> Result<> {
        static const QRegularExpression identifier("^[a-zA-Z][a-zA-Z0-9_]*$");
        if (identifier.match(name).hasMatch())
            return ResultOk;
        return ResultError(Tr::tr("Not a class name."));
    });

    sources.setQmlName("Sources");
    sources.setLabelText(Tr::tr("Sources"));

    sourceType.setQmlName("SourceType");
    sourceType.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
    sourceType.addOption(Tr::tr("Link library"));
    sourceType.addOption(Tr::tr("Include project"));
    sourceType.setValue(1);

    createSkeleton.setQmlName("CreateSkeleton");
    createSkeleton.setLabelText(Tr::tr("Create skeleton"));

    field(widgetLibrary, "WidgetLibrary", Tr::tr("Widget library:"));
    field(widgetProject, "WidgetProject", Tr::tr("Widget project file:"));
    field(widgetHeader, "WidgetHeader", Tr::tr("Widget header file:"));
    field(widgetSource, "WidgetSource", Tr::tr("Widget source file:"));
    field(widgetBaseClass, "WidgetBaseClass", Tr::tr("Widget base class:"));
    widgetBaseClass.setValue("QWidget");
    field(pluginClass, "PluginClass", Tr::tr("Plugin class name:"));
    field(pluginHeader, "PluginHeader", Tr::tr("Plugin header file:"));
    field(pluginSource, "PluginSource", Tr::tr("Plugin source file:"));

    iconFile.setQmlName("IconFile");
    iconFile.setLabelText(Tr::tr("Icon file:"));
    iconFile.setExpectedKind(PathChooserKind::File);
    iconFile.setHistoryCompleter("Qmake.Icon.History");
    iconFile.setPromptDialogTitle(Tr::tr("Select Icon"));
    iconFile.setPromptDialogFilter(
        Tr::tr("Icon files (*.png *.ico *.jpg *.xpm *.tif *.svg)"));

    description.setQmlName("Description");
    description.setLabelText(Tr::tr("Description"));
    field(group, "Group", Tr::tr("Group:"));
    field(toolTip, "ToolTip", Tr::tr("Tooltip:"));
    whatsThis.setQmlName("WhatsThis");
    whatsThis.setLabelText(Tr::tr("What's this:"));
    whatsThis.setDisplayStyle(StringAspect::TextEditDisplay);
    isContainer.setQmlName("IsContainer");
    isContainer.setLabelText(Tr::tr("The widget is a container"));

    propertyDefaults.setQmlName("PropertyDefaults");
    propertyDefaults.setLabelText(Tr::tr("Property defaults"));
    domXml.setQmlName("DomXml");
    domXml.setLabelText(Tr::tr("dom XML:"));
    domXml.setDisplayStyle(StringAspect::TextEditDisplay);

    // Everything a class is called follows from its name, until someone says
    // otherwise.
    connect(&className, &BaseAspect::changed, this, [this] {
        const QString name = className.value();
        widgetLibrary.setValue(name.toLower());
        widgetHeader.setValue(m_naming.headerFileName(name));
        pluginClass.setValue(name + QLatin1String("Plugin"));
        if (!m_domXmlIsTheirs)
            domXml.setValue(xmlFromClassName(name));
    });
    connect(&widgetLibrary, &BaseAspect::changed, this, [this] {
        widgetProject.setValue(
            widgetProjectFileName(widgetLibrary.value(), sourceType.value() == 0));
    });
    connect(&widgetHeader, &BaseAspect::changed, this, [this] {
        widgetSource.setValue(m_naming.headerToSourceFileName(widgetHeader.value()));
    });
    connect(&pluginClass, &BaseAspect::changed, this, [this] {
        pluginHeader.setValue(m_naming.headerFileName(pluginClass.value()));
    });
    connect(&pluginHeader, &BaseAspect::changed, this, [this] {
        pluginSource.setValue(m_naming.headerToSourceFileName(pluginHeader.value()));
    });
    connect(&domXml, &BaseAspect::changed, this, [this] {
        if (domXml.value() != xmlFromClassName(className.value()))
            m_domXmlIsTheirs = true;
    });

    connect(&sourceType, &BaseAspect::changed,
            this, &WidgetClassAspects::showWhatCanBeSaidAboutTheSources);
    connect(&createSkeleton, &BaseAspect::changed,
            this, &WidgetClassAspects::showWhatCanBeSaidAboutTheSources);
    showWhatCanBeSaidAboutTheSources();
}

void WidgetClassAspects::showWhatCanBeSaidAboutTheSources()
{
    const bool linkLibrary = sourceType.value() == 0;
    const bool skeleton = createSkeleton.value();

    widgetLibrary.setEnabled(linkLibrary);
    widgetSource.setEnabled(skeleton);
    widgetBaseClass.setEnabled(skeleton);
    // A project file is only asked for where there is a project to build:
    // either the widget is included rather than linked, or its sources are
    // being written out.
    widgetProject.setEnabled(!linkLibrary || skeleton);
    widgetProject.setValue(widgetProjectFileName(widgetProject.value(), linkLibrary));
}

PluginOptions::WidgetOptions WidgetClassAspects::widgetOptions() const
{
    PluginOptions::WidgetOptions wo;
    wo.createSkeleton = createSkeleton.value();
    wo.sourceType = sourceType.value() == 0 ? PluginOptions::WidgetOptions::LinkLibrary
                                            : PluginOptions::WidgetOptions::IncludeProject;
    wo.widgetLibrary = widgetLibrary.value();
    wo.widgetProjectFile = widgetProject.value();
    wo.widgetClassName = className.value();
    wo.widgetHeaderFile = widgetHeader.value();
    wo.widgetSourceFile = widgetSource.value();
    wo.widgetBaseClassName = widgetBaseClass.value();
    wo.pluginClassName = pluginClass.value();
    wo.pluginHeaderFile = pluginHeader.value();
    wo.pluginSourceFile = pluginSource.value();
    wo.iconFile = iconFile().toUrlishString();
    wo.group = group.value();
    wo.toolTip = toolTip.value();
    wo.whatsThis = whatsThis.value();
    wo.isContainer = isContainer.value();
    wo.domXml = domXml.value();
    return wo;
}

// The page: one line of explanation, and the list of widget classes with the
// current one's details beside it.
class CustomWidgetClassesAspects final : public AspectContainer
{
public:
    explicit CustomWidgetClassesAspects(const FileNamingParameters &naming);

    QList<std::shared_ptr<BaseAspect>> classItems() const { return classes.volatileItems(); }

    TextDisplay intro{this};
    AspectList classes{this};
};

CustomWidgetClassesAspects::CustomWidgetClassesAspects(const FileNamingParameters &naming)
{
    setAutoApply(true);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/QmakeProjectManager/CustomWidgetClassesPage.qml"));

    intro.setQmlName("Intro");
    intro.setText(Tr::tr("Specify the list of custom widgets and their properties."));

    classes.setQmlName("Classes");
    classes.setLabelText(Tr::tr("Widget Classes"));
    classes.setDisplayStyle(AspectList::DisplayStyle::ListViewWithDetails);
    classes.setCreateItemFunction([naming] { return std::make_shared<WidgetClassAspects>(naming); });
    classes.listViewDataCallback = [](WidgetClassAspects *item, int role) -> QVariant {
        if (role == Qt::DisplayRole)
            return item->className.value();
        return {};
    };
}

CustomWidgetWidgetsWizardPage::CustomWidgetWidgetsWizardPage(QWidget *parent)
    : QWizardPage(parent)
    , d(new CustomWidgetClassesAspects(m_fileNamingParameters))
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    if (QWidget *form = AspectWidgets::createAspectForm(d.get()))
        layout->addWidget(form);

    // A plugin needs at least one widget in it, so whether the page can be
    // left follows from the list.
    connect(&d->classes, &AspectList::volatileItemListChanged,
            this, &QWizardPage::completeChanged);

    setProperty(Utils::SHORT_TITLE_PROPERTY, Tr::tr("Custom Widgets"));
}

CustomWidgetWidgetsWizardPage::~CustomWidgetWidgetsWizardPage() = default;

bool CustomWidgetWidgetsWizardPage::isComplete() const
{
    return classCount() > 0;
}

void CustomWidgetWidgetsWizardPage::initializePage()
{
    emit completeChanged();
}

int CustomWidgetWidgetsWizardPage::classCount() const
{
    return int(d->classItems().size());
}

QString CustomWidgetWidgetsWizardPage::classNameAt(int i) const
{
    const QList<std::shared_ptr<BaseAspect>> items = d->classItems();
    if (i < 0 || i >= items.size())
        return {};
    return static_cast<WidgetClassAspects *>(items.at(i).get())->className.value();
}

QList<PluginOptions::WidgetOptions> CustomWidgetWidgetsWizardPage::widgetOptions() const
{
    QList<PluginOptions::WidgetOptions> rc;
    for (const std::shared_ptr<BaseAspect> &item : d->classItems())
        rc.push_back(static_cast<WidgetClassAspects *>(item.get())->widgetOptions());
    return rc;
}

#ifdef WITH_TESTS

class CustomWidgetWidgetsPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void testThePageDrawsWithTheQmlItNames()
    {
        CustomWidgetClassesAspects aspects{FileNamingParameters()};
        const Result<> rendered
            = Core::aspectFormRenders(&aspects, "CustomWidgetClassesPage.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatAWidgetIsCalledFollowsItsClass()
    {
        WidgetClassAspects item{FileNamingParameters("h", "cpp", true)};
        item.className.setValue("MyWidget");

        QCOMPARE(item.widgetLibrary.value(), QString("mywidget"));
        QCOMPARE(item.widgetHeader.value(), QString("mywidget.h"));
        QCOMPARE(item.widgetSource.value(), QString("mywidget.cpp"));
        QCOMPARE(item.pluginClass.value(), QString("MyWidgetPlugin"));
        QCOMPARE(item.pluginHeader.value(), QString("mywidgetplugin.h"));
        QCOMPARE(item.pluginSource.value(), QString("mywidgetplugin.cpp"));
        QCOMPARE(item.widgetProject.value(), QString("mywidget.pri"));
    }

    void testTheXmlFollowsTheClassUntilItIsWritten()
    {
        WidgetClassAspects item{FileNamingParameters()};
        item.className.setValue("First");
        QCOMPARE(item.domXml.value(), xmlFromClassName("First"));

        // Renaming still rewrites it, because nobody has said otherwise.
        item.className.setValue("Second");
        QCOMPARE(item.domXml.value(), xmlFromClassName("Second"));

        item.domXml.setValue("<widget class=\"Mine\"/>");
        item.className.setValue("Third");
        QVERIFY2(item.domXml.value() == "<widget class=\"Mine\"/>",
                 "the XML that was written by hand was overwritten by a rename");
    }

    void testWhatIsAskedAboutTheSources()
    {
        WidgetClassAspects item{FileNamingParameters()};

        // Included, no skeleton: nothing to link, nothing to write, but there
        // is a project to include it in.
        QVERIFY(!item.widgetLibrary.isEnabled());
        QVERIFY(!item.widgetSource.isEnabled());
        QVERIFY(!item.widgetBaseClass.isEnabled());
        QVERIFY(item.widgetProject.isEnabled());

        item.sourceType.setValue(0);
        QVERIFY(item.widgetLibrary.isEnabled());
        QVERIFY2(!item.widgetProject.isEnabled(),
                 "a project file was asked for with nothing to put in it");

        item.createSkeleton.setValue(true);
        QVERIFY(item.widgetSource.isEnabled());
        QVERIFY(item.widgetBaseClass.isEnabled());
        QVERIFY(item.widgetProject.isEnabled());
    }

    void testTheProjectFileSaysWhetherItIsLinked()
    {
        QCOMPARE(widgetProjectFileName("mywidget", true), QString("mywidget.pro"));
        QCOMPARE(widgetProjectFileName("mywidget", false), QString("mywidget.pri"));
        // Whatever it was called before, it keeps its base name.
        QCOMPARE(widgetProjectFileName("mywidget.pri", true), QString("mywidget.pro"));
    }

    void testAClassNameIsAnIdentifier()
    {
        WidgetClassAspects item{FileNamingParameters()};
        QVERIFY(item.className.validationMessage("MyWidget").isEmpty());
        QVERIFY2(!item.className.validationMessage("2Wrong").isEmpty(),
                 "a class name could start with a digit");
        QVERIFY(!item.className.validationMessage("has space").isEmpty());
        QVERIFY(!item.className.validationMessage({}).isEmpty());
    }
};

QObject *createCustomWidgetWidgetsPageTest()
{
    return new CustomWidgetWidgetsPageTest;
}

#endif // WITH_TESTS

} // namespace QmakeProjectManager::Internal

#ifdef WITH_TESTS
#include "customwidgetwidgetswizardpage.moc"
#endif

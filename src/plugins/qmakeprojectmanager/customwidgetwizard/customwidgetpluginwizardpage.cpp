// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "customwidgetpluginwizardpage.h"
#include "customwidgetwidgetswizardpage.h"
#include "pluginoptions.h"
#include "../qmakeprojectmanagertr.h"

#include <utils/aspects.h>
#include <utils/aspectwidgets.h>
#include <utils/wizard.h>

#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <coreplugin/dialogs/ioptionspage.h>
#include <QTest>
#endif

using namespace Utils;

namespace QmakeProjectManager::Internal {

// Determine name for Q_EXPORT_PLUGIN
static inline QString createPluginName(const QString &prefix)
{
    return prefix.toLower() + QLatin1String("plugin");
}

bool pluginPageIsComplete(const QString &pluginName, const QString &collectionClass,
                          int classCount)
{
    if (pluginName.isEmpty())
        return false;
    // A collection is complete only with class name
    return classCount <= 1 || !collectionClass.isEmpty();
}

// What the page asks about the library the widgets are packed into. The
// collection is only asked about when there is more than one widget to
// collect, which is what greying the three fields out came to.
class CustomWidgetPluginAspects final : public AspectContainer
{
public:
    CustomWidgetPluginAspects(const FileNamingParameters &naming);

    void setClassCount(int count, const QString &firstClassName);
    bool isComplete() const;

    TextDisplay intro{this};
    StringAspect collectionClass{this};
    StringAspect collectionHeader{this};
    StringAspect collectionSource{this};
    StringAspect pluginName{this};
    StringAspect resourceFile{this};

    // Set again on every init(): the page is built once and used for whatever
    // the previous page ended up with.
    const FileNamingParameters &naming;

private:
    int m_classCount = -1;
};

CustomWidgetPluginAspects::CustomWidgetPluginAspects(const FileNamingParameters &naming)
    : naming(naming)
{
    setAutoApply(true);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/QmakeProjectManager/CustomWidgetPluginPage.qml"));

    intro.setQmlName("Intro");
    intro.setText(Tr::tr("Specify the properties of the plugin library and the collection class."));

    const auto field = [](StringAspect &aspect, const char *qmlName, const QString &label) {
        aspect.setQmlName(QString::fromLatin1(qmlName));
        aspect.setLabelText(label);
        aspect.setDisplayStyle(StringAspect::LineEditDisplay);
    };
    field(collectionClass, "CollectionClass", Tr::tr("Collection class:"));
    field(collectionHeader, "CollectionHeader", Tr::tr("Collection header file:"));
    field(collectionSource, "CollectionSource", Tr::tr("Collection source file:"));
    field(pluginName, "PluginName", Tr::tr("Plugin name:"));
    field(resourceFile, "ResourceFile", Tr::tr("Resource file:"));
    resourceFile.setValue(Tr::tr("icons.qrc"));

    // The names follow from the class, and the source file from the header.
    connect(&collectionClass, &BaseAspect::changed, this, [this] {
        collectionHeader.setValue(this->naming.headerFileName(collectionClass.value()));
        pluginName.setValue(createPluginName(collectionClass.value()));
    });
    connect(&collectionHeader, &BaseAspect::changed, this, [this] {
        collectionSource.setValue(this->naming.headerToSourceFileName(collectionHeader.value()));
    });
}

void CustomWidgetPluginAspects::setClassCount(int count, const QString &firstClassName)
{
    m_classCount = count;
    // One widget needs no collection to be gathered into, and its plugin is
    // named after it.
    const bool collecting = count != 1;
    for (StringAspect *aspect : {&collectionClass, &collectionHeader, &collectionSource})
        aspect->setEnabled(collecting);

    collectionClass.setValue({});
    collectionHeader.setValue({});
    collectionSource.setValue({});
    pluginName.setValue(collecting ? QString() : createPluginName(firstClassName));
}

bool CustomWidgetPluginAspects::isComplete() const
{
    return pluginPageIsComplete(pluginName.value(), collectionClass.value(), m_classCount);
}

CustomWidgetPluginWizardPage::CustomWidgetPluginWizardPage(QWidget *parent)
    : QWizardPage(parent)
    , d(new CustomWidgetPluginAspects(m_fileNamingParameters))
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    if (QWidget *form = AspectWidgets::createAspectForm(d.get()))
        layout->addWidget(form);

    // Whether the page can be left follows from what is in it.
    for (StringAspect *aspect : {&d->pluginName, &d->collectionClass})
        connect(aspect, &BaseAspect::changed, this, &QWizardPage::completeChanged);

    setProperty(Utils::SHORT_TITLE_PROPERTY, Tr::tr("Plugin Details"));
}

CustomWidgetPluginWizardPage::~CustomWidgetPluginWizardPage() = default;

void CustomWidgetPluginWizardPage::init(const CustomWidgetWidgetsWizardPage *widgetsPage)
{
    const int count = widgetsPage->classCount();
    d->setClassCount(count, count == 1 ? widgetsPage->classNameAt(0) : QString());
    emit completeChanged();
}

std::shared_ptr<PluginOptions> CustomWidgetPluginWizardPage::basicPluginOptions() const
{
    std::shared_ptr<PluginOptions> po(new PluginOptions);
    po->pluginName = d->pluginName.value();
    po->resourceFile = d->resourceFile.value();
    po->collectionClassName = d->collectionClass.value();
    po->collectionHeaderFile = d->collectionHeader.value();
    po->collectionSourceFile = d->collectionSource.value();
    return po;
}

bool CustomWidgetPluginWizardPage::isComplete() const
{
    return d->isComplete();
}

#ifdef WITH_TESTS

class CustomWidgetPluginPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void testThePageDrawsWithTheQmlItNames()
    {
        FileNamingParameters naming;
        CustomWidgetPluginAspects aspects(naming);
        const Result<> rendered
            = Core::aspectFormRenders(&aspects, "CustomWidgetPluginPage.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhenThePageCanBeLeft()
    {
        // A plugin always needs a name; a collection of more than one widget
        // needs a class to gather them into, and one widget needs none.
        QVERIFY(!pluginPageIsComplete({}, {}, 1));
        QVERIFY(!pluginPageIsComplete({}, "Collection", 3));
        QVERIFY(pluginPageIsComplete("theplugin", {}, 1));
        QVERIFY2(!pluginPageIsComplete("theplugin", {}, 3),
                 "three widgets were collected into nothing");
        QVERIFY(pluginPageIsComplete("theplugin", "Collection", 3));
    }

    void testTheNamesFollowTheClass()
    {
        const FileNamingParameters naming("h", "cpp", true);
        CustomWidgetPluginAspects aspects(naming);

        aspects.collectionClass.setValue("MyCollection");
        QCOMPARE(aspects.collectionHeader.value(), QString("mycollection.h"));
        QCOMPARE(aspects.collectionSource.value(), QString("mycollection.cpp"));
        QCOMPARE(aspects.pluginName.value(), QString("mycollectionplugin"));
    }

    void testOneWidgetIsCollectedIntoNothing()
    {
        FileNamingParameters naming;
        CustomWidgetPluginAspects aspects(naming);

        aspects.setClassCount(1, "OnlyOne");
        QVERIFY2(!aspects.collectionClass.isEnabled(),
                 "a collection was asked for with one widget to collect");
        QVERIFY(!aspects.collectionHeader.isEnabled());
        QVERIFY(!aspects.collectionSource.isEnabled());
        // Named after the widget itself, so the page is done as it opens.
        QCOMPARE(aspects.pluginName.value(), QString("onlyoneplugin"));
        QVERIFY(aspects.isComplete());

        aspects.setClassCount(3, {});
        QVERIFY(aspects.collectionClass.isEnabled());
        QVERIFY2(aspects.pluginName.value().isEmpty(),
                 "the plugin kept the name of a widget that is no longer alone");
        QVERIFY(!aspects.isComplete());
    }
};

QObject *createCustomWidgetPluginPageTest()
{
    return new CustomWidgetPluginPageTest;
}

#endif // WITH_TESTS

} // namespace QmakeProjectManager::Internal

#ifdef WITH_TESTS
#include "customwidgetpluginwizardpage.moc"
#endif

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "configurationsaspect.h"

#include "beautifiertool.h"
#include "beautifiertr.h"

#include <utils/stringutils.h>

#ifdef WITH_TESTS
#include <coreplugin/icore.h>
#include <QTest>
#endif

using namespace Utils;

namespace Beautifier::Internal {

// The settings are the container as well as where the configurations live, so
// the aspect registers itself in them and the page reaches it by name.
ConfigurationsAspect::ConfigurationsAspect(AbstractSettings *settings)
    : AspectContainer(settings)
    , m_settings(settings)
{
    setQmlName("Configurations");

    current.setQmlName("Current");
    current.setLabelText(Tr::tr("Configuration:"));
    current.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

    name.setQmlName("Name");
    name.setLabelText(Tr::tr("Name:"));
    name.setDisplayStyle(StringAspect::LineEditDisplay);
    // What the configuration is stored under, so it has to be a file name.
    name.setValidationFunction([](const QString &text) -> Result<> {
        if (text.trimmed().isEmpty())
            return ResultError(Tr::tr("The name cannot be empty."));
        static const QString forbidden = R"(/\?><*%:")" "'";
        for (const QChar c : text) {
            if (forbidden.contains(c))
                return ResultError(Tr::tr("The name cannot contain %1.").arg(forbidden));
        }
        return ResultOk;
    });

    value.setQmlName("Value");
    value.setLabelText(Tr::tr("Value:"));
    value.setDisplayStyle(StringAspect::TextEditDisplay);

    documentation.setQmlName("Documentation");
    documentation.setTextFormat(AspectControls::TextFormat::RichText);
    documentation.setWordWrap(true);

    add.setQmlName("Add");
    add.setActionText(Tr::tr("Add"));
    add.setAction([this] {
        const QString added = uniqueName();
        m_settings->setStyle(added, QString());
        reload();
        setCurrentConfiguration(added);
    });

    remove.setQmlName("Remove");
    remove.setActionText(Tr::tr("Remove"));
    remove.setAction([this] {
        const QString gone = currentConfiguration();
        if (gone.isEmpty())
            return;
        m_settings->removeStyle(gone);
        reload();
    });

    // Behaviour, not layout.
    connect(&current, &BaseAspect::volatileValueChanged, this, [this] {
        showConfiguration(currentConfiguration());
    });
    name.addOnVolatileValueChanged(this, [this] { storeName(); });
    value.addOnVolatileValueChanged(this, [this] { storeValue(); });
}

QString ConfigurationsAspect::currentConfiguration() const
{
    return current.itemValue().toString();
}

void ConfigurationsAspect::setCurrentConfiguration(const QString &name)
{
    const int index = current.indexForItemValue(name);
    current.setValue(index < 0 ? 0 : index);
}

// Which configuration to show afterwards is the caller's - every one of them
// has just changed which that is.
void ConfigurationsAspect::reload()
{
    // The value editor completes against the options the tool documents, which
    // is a list nobody remembers.
    QStringList words = m_settings->options();
    words << m_settings->completerWords();
    words.sort(Qt::CaseInsensitive);
    value.setCompletions(words);

    current.clearOptions();
    for (const QString &style : m_settings->styles())
        current.addOption(SelectionAspect::Option(style, {}, style));

    current.setValue(0);
    showConfiguration(currentConfiguration());
}

void ConfigurationsAspect::showDocumentationFor(const QString &word)
{
    const QString doc = word.isEmpty() ? QString() : m_settings->documentation(word);
    if (doc.isEmpty()) {
        documentation.setText(QString());
        documentation.setVisible(false);
        return;
    }
    documentation.setText(Tr::tr("Documentation for \"%1\"").arg(word) + "<br>" + doc);
    documentation.setVisible(true);
}

void ConfigurationsAspect::showConfiguration(const QString &style)
{
    // Nothing is current while the form is being filled in, so nothing the
    // fields say on the way is written back.
    m_loaded.clear();
    const bool has = !style.isEmpty();
    name.setVisible(has);
    value.setVisible(has);
    remove.setEnabled(has);
    if (!has) {
        name.setValue(QString());
        value.setValue(QString());
        showDocumentationFor({});
        return;
    }

    // What was found in the installation is not the user's to change.
    const bool readOnly = m_settings->styleIsReadOnly(style);
    name.setReadOnly(readOnly);
    value.setReadOnly(readOnly);
    remove.setEnabled(!readOnly);

    name.setValue(style);
    value.setValue(m_settings->style(style));
    showDocumentationFor({});
    m_loaded = style;
}

void ConfigurationsAspect::storeName()
{
    if (m_loaded.isEmpty())
        return;
    const QString renamed = name.volatileValue().trimmed();
    if (renamed.isEmpty() || renamed == m_loaded || m_settings->styleExists(renamed))
        return;
    m_settings->replaceStyle(m_loaded, renamed, value.volatileValue());
    m_loaded = renamed;
    reload();
    setCurrentConfiguration(renamed);
}

void ConfigurationsAspect::storeValue()
{
    if (m_loaded.isEmpty())
        return;
    m_settings->setStyle(m_loaded, value.volatileValue());
}

QString ConfigurationsAspect::uniqueName() const
{
    return Utils::makeUniquelyNumbered(Tr::tr("New Configuration"), m_settings->styles());
}

#ifdef WITH_TESTS

// The configurations were a combo box with Add, Edit and Remove opening a
// modal dialog, and only the dialog's editor knew how to complete an option or
// explain one, so none of it could be read back without opening both.

// A settings object of its own, writing into a directory of its own: the real
// ones are singletons the rest of the plugin formats code with.
class TestSettings final : public AbstractSettings
{
public:
    TestSettings()
        : AbstractSettings("beautifier-test", ".cfg")
    {
        documentationFilePath = Core::ICore::userResourcePath(
            "beautifier-test-documentation.xml");
        documentationFilePath.writeFileContents(
            "<beautifier_documentation>"
            "<entry><keys><key>indent-classes</key></keys>"
            "<doc>Indent class blocks.</doc></entry>"
            "<entry><keys><key>pad-oper</key></keys>"
            "<doc>Pad operators with spaces.</doc></entry>"
            "</beautifier_documentation>");
    }

    ~TestSettings() override
    {
        for (const QString &style : styles())
            styleFileName(style).removeFile();
        documentationFilePath.removeFile();
    }
};

class ConfigurationsAspectTest : public QObject
{
    Q_OBJECT

private slots:
    void testTheConfigurationsAreTheOnesOnDisk();
    void testAddingMakesOneAndSelectsIt();
    void testEditingTheValueStoresIt();
    void testRenamingKeepsWhatWasInIt();
    void testShowingOneDoesNotWriteItBack();
    void testRemovingTakesItAway();
    void testTheEditorExplainsTheOptionUnderTheCursor();
    void testTheEditorCompletesAgainstTheToolsOptions();
};

void ConfigurationsAspectTest::testTheConfigurationsAreTheOnesOnDisk()
{
    TestSettings settings;
    ConfigurationsAspect configurations(&settings);
    settings.setStyle("alpha", "indent-classes");
    settings.setStyle("beta", "pad-oper");
    configurations.reload();

    QCOMPARE(configurations.current.optionCount(), 2);
    QCOMPARE(configurations.current.displayForIndex(0), QString("alpha"));
    // The name, not a position: adding or removing one moves the rest.
    configurations.setCurrentConfiguration("beta");
    QCOMPARE(configurations.currentConfiguration(), QString("beta"));
    QCOMPARE(configurations.value.volatileValue(), QString("pad-oper"));
}

void ConfigurationsAspectTest::testAddingMakesOneAndSelectsIt()
{
    TestSettings settings;
    ConfigurationsAspect configurations(&settings);
    configurations.reload();
    const int before = configurations.current.optionCount();

    configurations.add.triggerAction();
    QCOMPARE(configurations.current.optionCount(), before + 1);
    // The one that was added is the one being shown, so it can be typed into
    // straight away.
    const QString added = configurations.currentConfiguration();
    QVERIFY(!added.isEmpty());
    QCOMPARE(configurations.name.volatileValue(), added);
    QVERIFY(settings.styleExists(added));

    // And a second one does not collide with it.
    configurations.add.triggerAction();
    QVERIFY(configurations.currentConfiguration() != added);
}

void ConfigurationsAspectTest::testEditingTheValueStoresIt()
{
    TestSettings settings;
    ConfigurationsAspect configurations(&settings);
    configurations.reload();
    configurations.add.triggerAction();
    const QString added = configurations.currentConfiguration();

    configurations.value.setVolatileValue(QString("indent-classes\npad-oper"));
    QCOMPARE(settings.style(added), QString("indent-classes\npad-oper"));
}

void ConfigurationsAspectTest::testRenamingKeepsWhatWasInIt()
{
    TestSettings settings;
    ConfigurationsAspect configurations(&settings);
    settings.setStyle("before", "indent-classes");
    configurations.reload();
    configurations.setCurrentConfiguration("before");

    configurations.name.setVolatileValue(QString("after"));

    QVERIFY(!settings.styleExists("before"));
    QVERIFY(settings.styleExists("after"));
    QCOMPARE(settings.style("after"), QString("indent-classes"));
    QCOMPARE(configurations.currentConfiguration(), QString("after"));

    // A name that is not a name is refused, the way the dialog refused to
    // close on one.
    const BaseAspect &name = configurations.name;
    QVERIFY(!name.validationMessage("").isEmpty());
    QVERIFY(!name.validationMessage("with/slash").isEmpty());
    QCOMPARE(name.validationMessage("plain"), QString());
}

void ConfigurationsAspectTest::testShowingOneDoesNotWriteItBack()
{
    TestSettings settings;
    ConfigurationsAspect configurations(&settings);
    settings.setStyle("alpha", "indent-classes");
    settings.setStyle("beta", "pad-oper");
    configurations.reload();

    configurations.setCurrentConfiguration("alpha");
    configurations.setCurrentConfiguration("beta");
    configurations.setCurrentConfiguration("alpha");

    // Loading fills the name and the value field by field, which looks exactly
    // like the user typing - a half-loaded form is one configuration's name
    // beside another's contents.
    QCOMPARE(settings.style("alpha"), QString("indent-classes"));
    QCOMPARE(settings.style("beta"), QString("pad-oper"));
    QVERIFY(settings.styleExists("alpha"));
    QVERIFY(settings.styleExists("beta"));
}

void ConfigurationsAspectTest::testRemovingTakesItAway()
{
    TestSettings settings;
    ConfigurationsAspect configurations(&settings);
    settings.setStyle("alpha", "indent-classes");
    settings.setStyle("beta", "pad-oper");
    configurations.reload();
    configurations.setCurrentConfiguration("alpha");

    configurations.remove.triggerAction();

    QVERIFY(!settings.styleExists("alpha"));
    QCOMPARE(configurations.current.optionCount(), 1);
    QCOMPARE(configurations.currentConfiguration(), QString("beta"));
}

void ConfigurationsAspectTest::testTheEditorExplainsTheOptionUnderTheCursor()
{
    TestSettings settings;
    ConfigurationsAspect configurations(&settings);
    configurations.reload();

    // Nothing under the cursor, nothing to explain.
    configurations.showDocumentationFor({});
    QVERIFY(!configurations.documentation.isVisible());

    configurations.showDocumentationFor("indent-classes");
    QVERIFY(configurations.documentation.isVisible());
    QVERIFY2(configurations.documentation.text().contains("Indent class blocks."),
             qPrintable(configurations.documentation.text()));
    QVERIFY(configurations.documentation.text().contains("indent-classes"));

    // A word that is not an option is not an error either; there is simply
    // nothing to say about it.
    configurations.showDocumentationFor("not-an-option");
    QVERIFY(!configurations.documentation.isVisible());
}

void ConfigurationsAspectTest::testTheEditorCompletesAgainstTheToolsOptions()
{
    TestSettings settings;
    ConfigurationsAspect configurations(&settings);
    configurations.reload();

    // What a control would be given: the descriptor is how a renderer learns
    // what to complete against.
    const QStringList completions = configurations.value.presentation().completions;
    QVERIFY2(completions.contains("indent-classes"), qPrintable(completions.join(", ")));
    QVERIFY2(completions.contains("pad-oper"), qPrintable(completions.join(", ")));
}

QObject *createConfigurationsAspectTest()
{
    return new ConfigurationsAspectTest;
}

#endif // WITH_TESTS

} // namespace Beautifier::Internal

#include "configurationsaspect.moc"

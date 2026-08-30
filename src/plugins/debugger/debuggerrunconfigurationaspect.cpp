// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "debuggerrunconfigurationaspect.h"

#include "debuggertr.h"

#include <cppeditor/cppmodelmanager.h>

#include <coreplugin/helpmanager.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/environmentkitaspect.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/target.h>

#include <qtsupport/qtbuildaspects.h>

#include <utils/detailswidget.h>
#include <utils/environment.h>
#include <utils/layoutbuilder.h>

#include <QDebug>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace ProjectExplorer;
using namespace Utils;

namespace Debugger {

/*!
    \class Debugger::DebuggerRunConfigurationAspect
*/

static bool isDisabled(TriStateAspect *aspect)
{
    QTC_ASSERT(aspect, return false);
    return aspect->value() == TriState::Disabled;
}

DebuggerRunConfigurationAspect::DebuggerRunConfigurationAspect(BuildConfiguration *bc)
    : m_buildConfiguration(bc)
{
    setId("DebuggerAspect");
    setDisplayName(Tr::tr("Debugger Settings"));

    addDataExtractor(this, &DebuggerRunConfigurationAspect::useCppDebugger, &Data::useCppDebugger);
    addDataExtractor(this, &DebuggerRunConfigurationAspect::useQmlDebugger, &Data::useQmlDebugger);
    addDataExtractor(this, &DebuggerRunConfigurationAspect::usePythonDebugger, &Data::usePythonDebugger);
    addDataExtractor(this, &DebuggerRunConfigurationAspect::useMultiProcess, &Data::useMultiProcess);
    addDataExtractor(this, &DebuggerRunConfigurationAspect::overrideStartup, &Data::overrideStartup);

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Debugger/DebuggerRunSettings.qml"));

    m_cppAspect.setQmlName("CppDebugger");
    m_qmlAspect.setQmlName("QmlDebugger");
    m_pythonAspect.setQmlName("PythonDebugger");
    m_overrideStartupAspect.setQmlName("OverrideStartup");
    m_multiProcessAspect.setQmlName("MultiProcess");
    m_prerequisites.setQmlName("QmlPrerequisites");

    registerAspect(&m_cppAspect);
    registerAspect(&m_qmlAspect);
    registerAspect(&m_pythonAspect);
    registerAspect(&m_prerequisites);
    registerAspect(&m_overrideStartupAspect);
    registerAspect(&m_multiProcessAspect);

    m_prerequisites.setText(
        Tr::tr("<a href=\""
               "qthelp://org.qt-project.qtcreator/doc/creator-debugging-qml.html"
               "\">What are the prerequisites?</a>"));
    connect(&m_prerequisites, &Utils::TextDisplay::linkActivated,
            this, [](const QString &link) { Core::HelpManager::showHelpUrl(link); });

    // An option that only exists where the environment asks for it. The layout
    // used to decide that while it was being built.
    static const QString multiProcess = qtcEnvironmentVariable("QTC_DEBUGGER_MULTIPROCESS");
    m_multiProcessAspect.setVisible(multiProcess.toInt() != 0);

    // What the details widget showed as its summary, which is this group's
    // title.
    const auto updateSummary = [this] {
        const auto describe = [](const TriStateAspect &aspect, const QString &name) {
            if (aspect() == TriState::Enabled) {
                //: %1 is C++, QML, or Python
                return Tr::tr("Enable %1 debugger.").arg(name);
            }
            if (aspect() == TriState::Disabled) {
                //: %1 is C++, QML, or Python
                return Tr::tr("Disable %1 debugger.").arg(name);
            }
            //: %1 is C++, QML, or Python
            return Tr::tr("Try to determine need for %1 debugger.").arg(name);
        };

        setLabelText(QStringList{
            describe(m_cppAspect, "C++"),
            describe(m_qmlAspect, "QML"),
            describe(m_pythonAspect, "Python"),
            m_overrideStartupAspect().isEmpty()
                ? Tr::tr("No additional startup commands.")
                : Tr::tr("Use additional startup commands.")
        }.join(" "));
    };
    updateSummary();
    const QList<Utils::BaseAspect *> saySomething{&m_cppAspect, &m_qmlAspect, &m_pythonAspect,
                                                  &m_overrideStartupAspect};
    for (Utils::BaseAspect * const aspect : saySomething)
        connect(aspect, &Utils::BaseAspect::changed, this, updateSummary);

    m_cppAspect.setSettingsKey("RunConfiguration.UseCppDebugger");
    m_cppAspect.setLabelText(Tr::tr("C++ debugger:"));
    m_cppAspect.setOptionText(TriState::DefaultValue, Tr::tr("Automatic"));

    m_qmlAspect.setSettingsKey("RunConfiguration.UseQmlDebugger");
    m_qmlAspect.setLabelText(Tr::tr("QML debugger:"));
    m_qmlAspect.setOptionText(TriState::DefaultValue, Tr::tr("Automatic"));

    m_pythonAspect.setSettingsKey("RunConfiguration.UsePythonDebugger");
    m_pythonAspect.setLabelText(Tr::tr("Python debugger:"));
    m_pythonAspect.setOptionText(TriState::DefaultValue, Tr::tr("Automatic"));

    // Make sure at least one of the debuggers is set to be active.
    connect(&m_cppAspect, &TriStateAspect::changed, this, [this] {
        if (Utils::allOf({&m_cppAspect, &m_qmlAspect, &m_pythonAspect}, &isDisabled))
            m_qmlAspect.setValue(TriState::Default);
    });
    connect(&m_qmlAspect, &TriStateAspect::changed, this, [this] {
        if (Utils::allOf({&m_cppAspect, &m_qmlAspect, &m_pythonAspect}, &isDisabled))
            m_cppAspect.setValue(TriState::Default);
    });
    connect(&m_pythonAspect, &TriStateAspect::changed, this, [this] {
        if (Utils::allOf({&m_cppAspect, &m_qmlAspect, &m_pythonAspect}, &isDisabled))
            m_cppAspect.setValue(TriState::Default);
    });

    m_multiProcessAspect.setSettingsKey("RunConfiguration.UseMultiProcess");
    m_multiProcessAspect.setLabel(Tr::tr("Enable Debugging of Subprocesses"),
                                   BoolAspect::LabelPlacement::AtCheckBox);

    m_overrideStartupAspect.setSettingsKey("RunConfiguration.OverrideDebuggerStartup");
    m_overrideStartupAspect.setDisplayStyle(StringAspect::TextEditDisplay);
    m_overrideStartupAspect.setLabelText(Tr::tr("Additional startup commands:"));
}

DebuggerRunConfigurationAspect::~DebuggerRunConfigurationAspect() = default;

void DebuggerRunConfigurationAspect::setUseQmlDebugger(bool value)
{
    m_qmlAspect.setValue(value ? TriState::Enabled : TriState::Disabled);
}

bool DebuggerRunConfigurationAspect::useCppDebugger() const
{
    if (m_cppAspect() == TriState::Default) {
        if (m_buildConfiguration->project()->projectLanguages().contains(
                ProjectExplorer::Constants::CXX_LANGUAGE_ID)) {
            return true;
        }
        // If there is no other debugger, enable cpp debugger as a fallback to avoid leaving user
        // without any debugging support.
        bool otherDebuggerenabled = useQmlDebugger() || usePythonDebugger();
        return !otherDebuggerenabled;
    }
    return m_cppAspect() == TriState::Enabled;
}

static bool projectHasQmlDefines(ProjectExplorer::Project *project)
{
    auto projectInfo = CppEditor::CppModelManager::projectInfo(project);
    if (!projectInfo) // we may have e.g. a Python project
        return false;
    return Utils::anyOf(projectInfo->projectParts(),
                        [](const CppEditor::ProjectPart::ConstPtr &part){
                            return Utils::anyOf(part->projectMacros, [](const Macro &macro){
                                return macro.key == "QT_DECLARATIVE_LIB"
                                       || macro.key == "QT_QUICK_LIB"
                                       || macro.key == "QT_QML_LIB";
                            });
                        });
}

bool DebuggerRunConfigurationAspect::useQmlDebugger() const
{
    if (m_qmlAspect() == TriState::Default) {
        const Core::Context languages = m_buildConfiguration->project()->projectLanguages();
        if (!languages.contains(ProjectExplorer::Constants::QMLJS_LANGUAGE_ID))
            return projectHasQmlDefines(m_buildConfiguration->project());

        // Try to find a build configuration to check whether qml debugging is enabled there
        if (const auto aspect = m_buildConfiguration->aspect<QtSupport::QmlDebuggingAspect>())
            return aspect->value() == TriState::Enabled;

        return !languages.contains(ProjectExplorer::Constants::CXX_LANGUAGE_ID);
    }
    return m_qmlAspect() == TriState::Enabled;
}

bool DebuggerRunConfigurationAspect::usePythonDebugger() const
{
    if (m_pythonAspect() == TriState::Default) {
        const Core::Context languages = m_buildConfiguration->project()->projectLanguages();
        return languages.contains(ProjectExplorer::Constants::PYTHON_LANGUAGE_ID);
    }
    return m_pythonAspect() == TriState::Enabled;
}

bool DebuggerRunConfigurationAspect::useMultiProcess() const
{
    return m_multiProcessAspect();
}

void DebuggerRunConfigurationAspect::setUseMultiProcess(bool value)
{
    m_multiProcessAspect.setValue(value);
}

QString DebuggerRunConfigurationAspect::overrideStartup() const
{
    return m_overrideStartupAspect();
}

void DebuggerRunConfigurationAspect::toMap(Store &map) const
{
    m_cppAspect.toMap(map);
    m_qmlAspect.toMap(map);
    m_pythonAspect.toMap(map);
    m_multiProcessAspect.toMap(map);
    m_overrideStartupAspect.toMap(map);

    // compatibility to old settings
    map.insert("RunConfiguration.UseCppDebuggerAuto", m_cppAspect() == TriState::Default);
    map.insert("RunConfiguration.UseQmlDebuggerAuto", m_qmlAspect() == TriState::Default);
}

void DebuggerRunConfigurationAspect::fromMap(const Store &map)
{
    m_cppAspect.fromMap(map);
    m_qmlAspect.fromMap(map);
    m_pythonAspect.fromMap(map);

    // respect old project settings
    if (map.value("RunConfiguration.UseCppDebuggerAuto", false).toBool())
        m_cppAspect.setValue(TriState::Default);
    if (map.value("RunConfiguration.UseQmlDebuggerAuto", false).toBool())
        m_qmlAspect.setValue(TriState::Default);

    m_multiProcessAspect.fromMap(map);
    m_overrideStartupAspect.fromMap(map);
}

#ifdef WITH_TESTS

class DebuggerRunSettingsTest final : public QObject
{
    Q_OBJECT

private slots:
    // The aspect drew itself with a widget, so a Qt Quick page showed nothing
    // where a run configuration's debugger settings belong. It names a page
    // now; this checks the page finds every aspect it asks for, and that the
    // summary the details widget used to show is the group's title.
    void testTheRunSettingsPageDrawsWhatItNames()
    {
        DebuggerRunConfigurationAspect aspect(nullptr);

        const auto byName = [&aspect](const QString &name) -> Utils::BaseAspect * {
            for (Utils::BaseAspect * const sub : aspect.aspects()) {
                if (sub->qmlName() == name)
                    return sub;
            }
            return nullptr;
        };
        for (const QString &name : QStringList{"CppDebugger", "QmlDebugger", "PythonDebugger",
                                               "OverrideStartup", "MultiProcess",
                                               "QmlPrerequisites"}) {
            QVERIFY2(byName(name), qPrintable("the page asks for " + name + ", which is not there"));
        }

        // The link beside the QML row is a label with somewhere to go.
        QCOMPARE(int(byName("QmlPrerequisites")->presentation().control),
                 int(Utils::AspectControls::Label));
        QVERIFY(byName("QmlPrerequisites")->displayText().contains("qthelp://"));

        // What the collapsed settings said about themselves, which the closure
        // computed and which nothing outside it could.
        QVERIFY2(aspect.labelText().contains(Tr::tr("Try to determine need for %1 debugger.")
                                                 .arg("C++")),
                 qPrintable("the summary says: " + aspect.labelText()));
        aspect.m_cppAspect.setValue(TriState::Enabled);
        QVERIFY2(aspect.labelText().contains(Tr::tr("Enable %1 debugger.").arg("C++")),
                 qPrintable("the summary did not follow the setting: " + aspect.labelText()));
    }
};

QObject *createDebuggerRunSettingsTest()
{
    return new DebuggerRunSettingsTest;
}

#endif // WITH_TESTS

} // namespace Debugger

#ifdef WITH_TESTS
#include "debuggerrunconfigurationaspect.moc"
#endif

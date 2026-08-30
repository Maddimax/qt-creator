// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cocobuildstep.h"

#include "cocopluginconstants.h"
#include "cocotr.h"
#include "globalsettings.h"

#include <cmakeprojectmanager/cmakeprojectconstants.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>
#include <qmakeprojectmanager/qmakeprojectmanagerconstants.h>
#include <QtTaskTree/QTaskTree>
#include <utils/layoutbuilder.h>

#ifdef WITH_TESTS
#include <projectexplorer/projectconfiguration.h>
#include <utils/algorithm.h>
#include <QTest>
#endif


namespace Coco::Internal {

using namespace ProjectExplorer;

CocoBuildStep *CocoBuildStep::create(BuildConfiguration *buildConfig)
{
    // The "new" command creates a small memory leak which we can tolerate.
    return new CocoBuildStep(
        new BuildStepList(buildConfig, Constants::COCO_STEP_ID), Utils::Id(Constants::COCO_STEP_ID));
}

CocoBuildStep::CocoBuildStep(ProjectExplorer::BuildStepList *bsl, Utils::Id id)
    : BuildStep(bsl, id)
{
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Coco/CocoBuildStep.qml"));

    m_toggleCoverage.setQmlName("ToggleCoverage");
    m_toggleCoverage.setAction([this] { onButtonClicked(); });

    // What the button says depends on the Coco installation and on the
    // project's settings, so it is worked out when a form is about to show it
    // as well as when the build system changes.
    connect(this, &Utils::AspectContainer::shown, this, [this] { updateDisplay(); });
}

bool CocoBuildStep::init()
{
    return true;
}

void CocoBuildStep::buildSystemUpdated()
{
    updateDisplay();
}

void CocoBuildStep::onButtonClicked()
{
    QTC_ASSERT(m_buildSettings, return);

    m_valid = !m_valid;

    setSummaryText(Tr::tr("Coco Code Coverage: Reconfiguring..."));
    m_toggleCoverage.setEnabled(false);

    m_buildSettings->setCoverage(m_valid);
    m_buildSettings->provideFile();
    m_buildSettings->reconfigure();
}

void CocoBuildStep::updateDisplay()
{
    QTC_ASSERT(m_buildSettings, return);

    if (!cocoSettings().isValid()) {
        setSummaryText("<i>" + Tr::tr("Coco Code Coverage: No working Coco installation.") + "</i>");
        m_toggleCoverage.setEnabled(false);
        return;
    }

    m_valid = m_buildSettings->validSettings();

    if (m_valid) {
        setSummaryText("<b>" + Tr::tr("Coco Code Coverage: Enabled.") + "</b>");
        m_toggleCoverage.setEnabled(true);
        m_toggleCoverage.setActionText(Tr::tr("Disable Coverage"));
    } else {
        setSummaryText(Tr::tr("Coco Code Coverage: Disabled."));
        m_toggleCoverage.setEnabled(true);
        m_toggleCoverage.setActionText(Tr::tr("Enable Coverage"));
    }
}

void CocoBuildStep::display()
{
    if (!m_buildSettings.isNull())
        return;

    m_buildSettings = BuildSettings::createdFor(buildConfiguration());
    m_buildSettings->read();
    m_buildSettings->connectToBuildStep(this);

    setImmutable(true);
    updateDisplay();
}

QtTaskTree::GroupItem CocoBuildStep::runRecipe()
{
    return QtTaskTree::GroupItem({});
}

// Factories

class QMakeStepFactory final : public BuildStepFactory
{
public:
    QMakeStepFactory()
    {
        registerStep<CocoBuildStep>(Utils::Id{Constants::COCO_STEP_ID});
        setSupportedProjectType(QmakeProjectManager::Constants::QMAKEPROJECT_ID);
        setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD);
        setRepeatable(false);
        setExtraInit([](BuildStep *step) { dynamic_cast<CocoBuildStep *>(step)->display(); });
    }
};

class CMakeStepFactory final : public BuildStepFactory
{
public:
    CMakeStepFactory()
    {
        registerStep<CocoBuildStep>(Utils::Id{Constants::COCO_STEP_ID});
        setSupportedProjectType(CMakeProjectManager::Constants::CMAKE_PROJECT_ID);
        setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD);
        setRepeatable(false);
        setExtraInit([](BuildStep *step) { dynamic_cast<CocoBuildStep *>(step)->display(); });
    }
};

static void addBuildStep(Target *target)
{
    for (BuildConfiguration *config : target->buildConfigurations()) {
        if (BuildSettings::supportsBuildConfig(*config)) {
            BuildStepList *steps = config->buildSteps();

            if (!steps->contains(Constants::COCO_STEP_ID))
                steps->insertStep(0, CocoBuildStep::create(config));

            steps->firstOfType<CocoBuildStep>()->display();
        }
    }
}

#ifdef WITH_TESTS

class CocoBuildStepPageTest final : public QObject
{
    Q_OBJECT

private slots:
    // A CocoBuildStep needs a build configuration, so its page is checked
    // against a stand-in holding the same aspect under the same name: that the
    // page finds it, that the form is that page rather than the generic list,
    // and that the aspect wants the control the page's delegate draws.
    void testThePageDrawsWhatItNames()
    {
        Utils::AspectContainer page;
        page.setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Coco/CocoBuildStep.qml"));

        Utils::ActionAspect toggle(&page);
        toggle.setQmlName("ToggleCoverage");
        toggle.setActionText("Enable Coverage");

        QStringList complaints;
        static QStringList *collected = nullptr;
        collected = &complaints;
        const auto previous = qInstallMessageHandler(nullptr);
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
            // Only what a page says about itself: an unrelated Qt warning from
            // whatever ran before is not this page complaining.
            if (collected && (type == QtWarningMsg || type == QtCriticalMsg)
                && msg.contains("qrc:/qt/qml/QtCreator")) {
                collected->append(msg);
            }
        });

        const std::unique_ptr<QWidget> form(ProjectExplorer::createAspectsForm(&page));
        if (form) {
            form->resize(400, 200);
            form->show();
            QTest::qWaitForWindowExposed(form.get());
        }
        collected = nullptr;
        qInstallMessageHandler(previous);

        QVERIFY(form);
        QVERIFY2(complaints.isEmpty(),
                 qPrintable("the Coco build step page complains: " + complaints.join("; ")));

        const QList<QWidget *> children = form->findChildren<QWidget *>();
        QWidget * const quick = Utils::findOr(children, nullptr, [](QWidget *child) {
            return qstrcmp(child->metaObject()->className(), "QQuickWidget") == 0;
        });
        QVERIFY2(quick, "the Coco build step page was not drawn with Qt Quick at all");
        QCOMPARE(quick->property("source").toUrl(), page.qmlSource());

        // Drawn with ButtonDelegate; AspectItems.qml maps that control to it.
        QCOMPARE(int(toggle.presentation().control), int(Utils::AspectControls::Button));
    }
};

QObject *createCocoBuildStepPageTest()
{
    return new CocoBuildStepPageTest;
}

#endif // WITH_TESTS

void setupCocoBuildSteps()
{
    static QMakeStepFactory theQmakeStepFactory;
    static CMakeStepFactory theCmakeStepFactory;

    QObject::connect(ProjectManager::instance(), &ProjectManager::projectAdded, [](Project *project) {
        if (Target *target = project->activeTarget()) {
            addBuildStep(target);

            QObject::connect(target, &Target::addedBuildConfiguration, [](BuildConfiguration *bc) {
                addBuildStep(bc->target());
            });
        }

        QObject::connect(project, &Project::addedTarget, [](Target *target) {
            addBuildStep(target);
        });
    });
}

} // namespace Coco::Internal

#ifdef WITH_TESTS
#include "cocobuildstep.moc"
#endif

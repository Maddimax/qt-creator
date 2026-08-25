// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtbuildaspects.h"

#include "baseqtversion.h"
#include "qtkitaspect.h"
#include "qtversionfactory.h"
#include "qtversionmanager.h"
#include "qtsupporttr.h"

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildpropertiessettings.h>
#include <projectexplorer/kitmanager.h>

#include <utils/qtcassert.h>

#ifdef WITH_TESTS
#include <QLibraryInfo>
#include <QTest>
#endif


using namespace ProjectExplorer;
using namespace Utils;

namespace QtSupport {

QString qmlDebuggingWarning(Kit *kit, TriState value, bool *supported)
{
    QString warning;
    *supported = QtVersion::isQmlDebuggingSupported(kit, &warning);
    if (!*supported)
        return warning;
    // What it costs to have it on. Nothing to say when it is off.
    if (value == TriState::Enabled) {
        return Tr::tr("Might make your application vulnerable.<br/>"
                      "Only use in a safe environment.");
    }
    return {};
}

QString qtQuickCompilerWarning(Kit *kit, TriState value, TriState qmlDebugging, bool *supported)
{
    QString warning;
    *supported = QtVersion::isQtQuickCompilerSupported(kit, &warning);
    if (!*supported)
        return warning;
    if (value != TriState::Enabled || qmlDebugging != TriState::Enabled)
        return {};
    // Before Qt 6 the two cannot both be on, and the compiler is what wins.
    QtVersion * const qtVersion = QtKitAspect::qtVersion(kit);
    if (qtVersion && qtVersion->qtVersion() < QVersionNumber(6, 0, 0))
        return Tr::tr("Disables QML debugging. QML profiling will still work.");
    return {};
}

QmlDebuggingAspect::QmlDebuggingAspect(BuildConfiguration *buildConfig)
    : TriStateAspect(buildConfig)
    , m_warning(buildConfig)
{
    setSettingsKey("EnableQmlDebugging");
    setLabelText(Tr::tr("QML debugging and profiling:"));

    m_warning.setIconType(InfoType::Warning);
    // The text has a <br/> in it, and is as long as it is: a warning that is
    // elided says nothing.
    m_warning.setTextFormat(AspectControls::TextFormat::RichText);
    m_warning.setWordWrap(true);

    // Behaviour, not layout. Whether the kit can debug QML is the kit's
    // answer and it changes while the page is open - and it decided the value
    // as well, which used to happen only once somebody opened the page.
    connect(KitManager::instance(), &KitManager::kitsChanged, this,
            &QmlDebuggingAspect::updateWarning);
    connect(buildConfig, &BuildConfiguration::kitChanged, this,
            &QmlDebuggingAspect::updateWarning);
    connect(this, &BaseAspect::changed, this, &QmlDebuggingAspect::updateWarning);

    setValue(buildPropertiesSettings().qmlDebugging());
    updateWarning();
}

void QmlDebuggingAspect::updateWarning()
{
    auto const buildConfig = qobject_cast<BuildConfiguration *>(container());
    QTC_ASSERT(buildConfig, return);
    Kit * const kit = buildConfig->kit();
    QTC_ASSERT(kit, return);

    bool supported = false;
    const QString warningText = qmlDebuggingWarning(kit, value(), &supported);
    if (!supported)
        setValue(TriState::Default);
    m_warning.setText(warningText);
    m_warning.setVisible(!warningText.isEmpty());
    setEnabled(supported);
}

QtQuickCompilerAspect::QtQuickCompilerAspect(BuildConfiguration *buildConfig)
    : TriStateAspect(buildConfig)
    , m_warning(buildConfig)
{
    setSettingsKey("QtQuickCompiler");
    setLabelText(Tr::tr("Qt Quick Compiler:"));

    m_warning.setIconType(InfoType::Warning);
    m_warning.setWordWrap(true);

    connect(KitManager::instance(), &KitManager::kitsChanged, this,
            &QtQuickCompilerAspect::updateWarning);
    connect(buildConfig, &BuildConfiguration::kitChanged, this,
            &QtQuickCompilerAspect::updateWarning);
    connect(this, &BaseAspect::changed, this, &QtQuickCompilerAspect::updateWarning);
    // What it warns about is what the other one is set to, so it has to hear
    // about that too.
    if (auto qmlDebugging = buildConfig->aspect<QmlDebuggingAspect>()) {
        connect(qmlDebugging, &BaseAspect::changed, this,
                &QtQuickCompilerAspect::updateWarning);
    }

    setValue(buildPropertiesSettings().qtQuickCompiler());
    updateWarning();
}

void QtQuickCompilerAspect::updateWarning()
{
    auto const buildConfig = qobject_cast<BuildConfiguration *>(container());
    QTC_ASSERT(buildConfig, return);
    Kit * const kit = buildConfig->kit();
    QTC_ASSERT(kit, return);

    const auto qmlDebugging = buildConfig->aspect<QmlDebuggingAspect>();
    bool supported = false;
    const QString warningText = qtQuickCompilerWarning(
        kit, value(), qmlDebugging ? qmlDebugging->value() : TriState::Default, &supported);
    if (!supported)
        setValue(TriState::Default);
    m_warning.setText(warningText);
    m_warning.setVisible(supported && !warningText.isEmpty());
    setVisible(supported);
}

#ifdef WITH_TESTS
class QtBuildAspectsTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanupTestCase()
    {
        // The manager owns it once it has been added, and the rest of the run
        // must not see a Qt this test invented.
        if (m_qt)
            QtVersionManager::removeVersion(m_qt);
    }

private:
    // A kit with a real Qt on it: the one this Creator was built against,
    // which is the only Qt a test run can count on being installed. The
    // machine's own kits are not - a test run starts from empty settings.
    Kit *kitWithQt()
    {
        if (m_kit.hasValue(QtKitAspect::id()))
            return &m_kit;
        const FilePath qmake
            = FilePath::fromString(QLibraryInfo::path(QLibraryInfo::BinariesPath))
                  .pathAppended("qmake")
                  .withExecutableSuffix();
        if (!qmake.isExecutableFile())
            return nullptr;
        m_qt = QtVersionFactory::createQtVersionFromQMakePath(qmake, DetectionSource::Manual);
        if (!m_qt || !m_qt->isValid()) {
            delete m_qt;
            m_qt = nullptr;
            return nullptr;
        }
        // The kit stores the Qt's id and looks it up in the manager, so a Qt
        // the manager has never heard of is no Qt at all.
        QtVersionManager::addVersion(m_qt);
        QtKitAspect::setQtVersion(&m_kit, m_qt);
        return &m_kit;
    }

    Kit m_kit;
    QtVersion *m_qt = nullptr;

private slots:
    void testAKitThatCannotDoItSaysSo()
    {
        // A kit with no Qt in it cannot debug QML or run the compiler, and the
        // reason is what the row underneath the setting shows. The aspects
        // used to work this out only when somebody opened the build settings,
        // which is also when they forced the value back to Default.
        Kit bare;
        bool supported = true;
        QVERIFY(!qmlDebuggingWarning(&bare, TriState::Enabled, &supported).isEmpty());
        QVERIFY(!supported);

        supported = true;
        QVERIFY(!qtQuickCompilerWarning(&bare, TriState::Enabled, TriState::Default, &supported)
                     .isEmpty());
        QVERIFY(!supported);
    }

    void testAKitThatCanSaysNothingUntilItIsTurnedOn()
    {
        Kit * const kit = kitWithQt();
        if (!kit)
            QSKIP("No Qt installation to build a kit from");

        bool supported = false;
        // Off, or left to the project: nothing to say.
        QVERIFY(qmlDebuggingWarning(kit, TriState::Default, &supported).isEmpty());
        QVERIFY(supported);
        QVERIFY(qmlDebuggingWarning(kit, TriState::Disabled, &supported).isEmpty());

        // On: what it costs.
        QVERIFY(!qmlDebuggingWarning(kit, TriState::Enabled, &supported).isEmpty());
        QVERIFY(supported);
    }

    void testTheCompilerOnlyWarnsAboutQmlDebuggingBeforeQt6()
    {
        Kit * const kit = kitWithQt();
        if (!kit)
            QSKIP("No Qt installation to build a kit from");
        QtVersion * const qt = QtKitAspect::qtVersion(kit);
        QVERIFY(qt);

        bool supported = false;
        // The two only conflict when both are on.
        QVERIFY(qtQuickCompilerWarning(kit, TriState::Enabled, TriState::Default, &supported)
                    .isEmpty());
        QVERIFY(qtQuickCompilerWarning(kit, TriState::Default, TriState::Enabled, &supported)
                    .isEmpty());

        const QString bothOn
            = qtQuickCompilerWarning(kit, TriState::Enabled, TriState::Enabled, &supported);
        if (qt->qtVersion() < QVersionNumber(6, 0, 0))
            QVERIFY(!bothOn.isEmpty());
        else
            QVERIFY2(bothOn.isEmpty(), qPrintable(bothOn));
    }
};

QObject *createQtBuildAspectsTest()
{
    return new QtBuildAspectsTest;
}
#endif // WITH_TESTS

} // namespace QtSupport

#ifdef WITH_TESTS
#include "qtbuildaspects.moc"
#endif

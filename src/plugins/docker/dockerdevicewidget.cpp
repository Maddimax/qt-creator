// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dockerdevicewidget.h"

#include "dockerapi.h"
#include "dockerdevice.h"
#include "dockersettings.h"
#include "dockertr.h"

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <projectexplorer/kitaspect.h>

#include <utils/commandline.h>
#include <utils/guiutils.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/utilsicons.h>

#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>

using namespace ProjectExplorer;
using namespace Utils;

namespace Docker::Internal {

DockerDeviceWidget::DockerDeviceWidget(const IDevice::Ptr &device)
    : IDeviceWidget(device)
{
    auto dockerDevice = std::dynamic_pointer_cast<DockerDevice>(device);
    QTC_ASSERT(dockerDevice, return);

    m_api = DockerApi::instance(dockerDevice->type());
    QTC_ASSERT(m_api, return);

    using namespace Layouting;

    // What the daemon is doing is the device's to say; the button that makes
    // it be asked again is part of the same row.
    connect(m_api, &DockerApi::dockerDaemonAvailableChanged, this, [dockerDevice] {
        dockerDevice->daemonState.updateSummary();
    });
    dockerDevice->daemonState.updateSummary();

    onFirstShow(this, [this, dockerDevice] {
        const FilePath dockerExe = m_api->dockerClient();
        if (dockerExe.isEmpty())
            return;

        const auto onSetup = [dockerExe, dockerDevice](Process &process) {
            process.setCommand({dockerExe, {"images", "-q", dockerDevice->repoAndTag()}});
        };
        const auto onDone = [dockerDevice](const Process &process) {
            const QString imageId = process.cleanedStdOut().trimmed();
            if (process.exitCode() == 0 && !imageId.isEmpty())
                dockerDevice->imageId.setValue(imageId);
        };
        m_imageIdRunner.start({ProcessTask(onSetup, onDone)});
    });
    dockerDevice->refreshNetworks.setToolTip(
        Tr::tr("Refresh %1 networks").arg(m_api->displayType()));

    using namespace Layouting;

    // clang-format off
    Column {
        noMargin,
        Form {
            noMargin,
            dockerDevice->repo, br,
            dockerDevice->tag, br,
            dockerDevice->imageId, br,
            dockerDevice->daemonState, br,
            dockerDevice->useLocalUidGid, br,
            dockerDevice->keepEntryPoint, br,
            dockerDevice->enableLldbFlags, br,
            dockerDevice->mountCmdBridge, br,
            dockerDevice->enableX11Forwarding, br,
            dockerDevice->x11Display, br,
            dockerDevice->network, dockerDevice->refreshNetworks, br,
            dockerDevice->extraArgs, br,
            dockerDevice->environment, br,
            dockerDevice->mounts, br,
            empty, dockerDevice->mountsWarning, br,
            Tr::tr("Port mappings:"), dockerDevice->portMappings, br,
            dockerDevice->createCommandLineDisplay, br,
            dockerDevice->deviceToolsGui(), br,
            Span(2, Row {
                dockerDevice->autoDetectKitItems,
                dockerDevice->removeAutoDetectedKitItems,
                dockerDevice->listAutoDetectedKitItems,
                st,
            }), br,
            dockerDevice->detectionLog
        }, br,
    }.attachTo(this);
    // clang-format on

    connect(&dockerDevice->mounts, &FilePathListAspect::volatileValueChanged,
            this, checkSettingsDirty);

    installMarkSettingsDirtyTriggerRecursively(this);
}

#ifdef WITH_TESTS
class DockerDeviceAspectsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDeviceSaysWhatItKnowsAboutItself()
    {
        // Four things the settings widget worked out and drew itself: whether
        // the daemon is up, that there is nothing to mount, what the docker
        // create call comes to, and the button that re-asks for networks.
        // Nothing but that widget could see any of them.
        const DockerDevice::Ptr device = DockerDevice::create(&dockerSettings());
        QVERIFY(device);

        // The daemon state is a summary plus the button that has it looked at
        // again - not a label, a tool button and a hand-written update.
        const AspectPresentation daemon = device->daemonState.presentation();
        QCOMPARE(daemon.control, AspectControls::TextWithAction);
        QVERIFY(!daemon.labelText.isEmpty());
        device->daemonState.updateSummary();
        QVERIFY2(!device->daemonState.displayText().isEmpty(),
                 "the device says nothing about its daemon");

        // The warning follows the mounts, whichever way round they go.
        device->mounts.setValue(QStringList{});
        QVERIFY(device->mountsWarning.isVisible());
        device->mounts.setValue(QStringList{"/tmp"});
        QVERIFY(!device->mountsWarning.isVisible());

        // What the settings come to, kept up as they change.
        device->createCommandLineDisplay.setText({});
        device->repo.setValue("alpine");
        QVERIFY2(!device->createCommandLineDisplay.text().isEmpty(),
                 "the device does not say what it would run");
        QVERIFY(!device->createCommandLineDisplay.labelText().isEmpty());

        // A button that says what it does with a picture: it had a reload icon
        // and no text.
        const AspectPresentation refresh = device->refreshNetworks.presentation();
        QCOMPARE(refresh.control, AspectControls::Button);
        QVERIFY2(!refresh.actionIcon.isNull(), "the refresh button has nothing to show");
    }

    void testDetectingKitItemsIsSomethingTheDeviceDoes()
    {
        // The detection recipe, the three buttons that start it and the log it
        // writes to lived in the settings widget, so they went away with it and
        // only a widget page could show any of them.
        const DockerDevice::Ptr device = DockerDevice::create(&dockerSettings());
        QVERIFY(device);

        for (const ActionAspect * const button : {&device->autoDetectKitItems,
                                                  &device->removeAutoDetectedKitItems,
                                                  &device->listAutoDetectedKitItems}) {
            const AspectPresentation p = button->presentation();
            QCOMPARE(p.control, AspectControls::Button);
            QVERIFY(!p.actionText.isEmpty());
            QVERIFY(button->isEnabled());
        }

        // The log is read, not typed into.
        const AspectPresentation log = device->detectionLog.presentation();
        QCOMPARE(log.control, AspectControls::TextEdit);
        QVERIFY(log.readOnly);
        QVERIFY(!log.labelText.isEmpty());

        // A run starts from a clean log rather than appending to what the last
        // one said. What it finds depends on the machine; that it starts over
        // does not. Listing is the one that needs no container to run.
        device->detectionLog.setValue("what the last run said");
        device->listAutoDetectedKitItems.triggerAction();
        QVERIFY2(!device->detectionLog.volatileValue().contains("what the last run said"),
                 "a run appended to the previous run's log");

        // Auto-detect says why it cannot start rather than nothing at all: on
        // a machine with no daemon it fails at the container.
        device->detectionLog.setValue({});
        device->autoDetectKitItems.triggerAction();
        QVERIFY2(!device->detectionLog.volatileValue().isEmpty(),
                 "auto-detect said nothing about what it did");
    }
};

QObject *createDockerDeviceAspectsTest()
{
    return new DockerDeviceAspectsTest;
}
#endif // WITH_TESTS

} // namespace Docker::Internal

#ifdef WITH_TESTS
#include "dockerdevicewidget.moc"
#endif

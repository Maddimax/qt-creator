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

    auto logView = new QTextBrowser;

    auto autoDetectButton = new QPushButton(Tr::tr("Auto-detect Kit Items"));
    auto undoAutoDetectButton = new QPushButton(Tr::tr("Remove Auto-Detected Kit Items"));
    auto listAutoDetectedButton = new QPushButton(Tr::tr("List Auto-Detected Kit Items"));
    const QList<QWidget *> tempDisabledWidgets = {autoDetectButton, undoAutoDetectButton,
                                                  listAutoDetectedButton};
    connect(autoDetectButton,
            &QPushButton::clicked,
            this,
            [this, logView, dockerDevice, tempDisabledWidgets] {
                logView->clear();
                Result<> startResult = dockerDevice->updateContainerAccess();

                if (!startResult) {
                    logView->append(Tr::tr("Failed to start container."));
                    logView->append(startResult.error());
                    return;
                }

                const auto log = [logView](const QString &msg) { logView->append(msg); };
                // clang-format off
                const QtTaskTree::Group recipe {
                    dockerDevice->autoDetectDeviceToolsRecipe(),
                    ProjectExplorer::removeDetectedKitsRecipe(dockerDevice, log),
                    ProjectExplorer::kitDetectionRecipe(dockerDevice, DetectionSource::FromSystem, log)
                };
                // clang-format on

                const auto onTaskTreeSetup = [logView, tempDisabledWidgets] {
                    for (QWidget *widget : tempDisabledWidgets)
                        widget->setEnabled(false);
                    logView->append(Tr::tr("Starting auto-detection..."));
                };

                const auto onTaskTreeDone = [logView, tempDisabledWidgets] {
                    for (QWidget *widget : tempDisabledWidgets)
                        widget->setEnabled(true);
                    logView->append(Tr::tr("Done."));
                };

                m_detectionRunner.start(recipe, onTaskTreeSetup, onTaskTreeDone);

                if (m_api->dockerDaemonAvailable().value_or(false) == false)
                    logView->append(
                        Tr::tr("%1 daemon appears to be stopped.").arg(m_api->displayType()));
                else
                    logView->append(
                        Tr::tr("%1 daemon appears to be running.").arg(m_api->displayType()));
                dockerDevice->daemonState.updateSummary();
            });

    connect(undoAutoDetectButton, &QPushButton::clicked, this, [this, logView, device] {
        logView->clear();
        m_detectionRunner.start(
            ProjectExplorer::removeDetectedKitsRecipe(device, [logView](const QString &msg) {
                logView->append(msg);
            })
        );
    });

    connect(listAutoDetectedButton, &QPushButton::clicked, this, [logView, device] {
        logView->clear();
        listAutoDetected(device, [logView](const QString &msg) { logView->append(msg); });
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
                autoDetectButton,
                undoAutoDetectButton,
                listAutoDetectedButton,
                st,
            }), br,
            Tr::tr("Detection log:"), logView
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

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "sshkeycreation.h"

#include "sshkeycreationdialog.h"

#include <coreplugin/icore.h>

#include <projectexplorer/devicesupport/sshparameters.h>

#include <QDialog>

#ifdef WITH_TESTS
#include "remotelinux_constants.h"

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/idevicefactory.h>

#include <QTest>
#endif

using namespace ProjectExplorer;

namespace Remote::Internal {

void setupSshKeyCreation(SshParametersAspectContainer &ssh)
{
    ssh.createKey.setVisible(true);
    ssh.createKey.setAction([&ssh] {
        SshKeyCreationDialog dialog(Core::ICore::dialogParent());
        if (dialog.exec() == QDialog::Accepted)
            ssh.privateKeyFile.setValue(dialog.privateKeyFilePath());
    });
}

#ifdef WITH_TESTS
class SshKeyCreationTest final : public QObject
{
    Q_OBJECT

private slots:
    void testBothRemoteDevicesOfferTheSameKeyCreation()
    {
        // Linux and Windows each built the same "Create New..." QPushButton
        // and wired it to the same dialog. The container offers it now, and
        // this plugin is what makes the offer real - a device from anywhere
        // else has an SSH container with nothing to click.
        for (const Utils::Id type : {Utils::Id(Constants::GenericLinuxOsType),
                                     Utils::Id(Constants::GenericWindowsOsType)}) {
            IDeviceFactory * const factory = IDeviceFactory::find(type);
            QVERIFY2(factory, qPrintable(type.toString()));
            const IDevice::Ptr device = factory->construct();
            QVERIFY(device);

            SshParametersAspectContainer &ssh = device->sshParametersAspectContainer();
            const Utils::AspectPresentation p = ssh.createKey.presentation();
            QCOMPARE(p.control, Utils::AspectControls::Button);
            QVERIFY2(!p.actionText.isEmpty(), "the offer has no label");
            QVERIFY2(ssh.createKey.isVisible(),
                     qPrintable(type.toString() + " does not offer to make a key"));
        }
    }

    void testAContainerWithNoPluginBehindItOffersNothing()
    {
        // The descriptor lives in ProjectExplorer, which cannot make a key.
        // Without this plugin installing the action there is nothing to show,
        // rather than a button that asserts when pressed.
        SshParametersAspectContainer bare;
        QVERIFY(!bare.createKey.isVisible());
    }
};

QObject *createSshKeyCreationTest()
{
    return new SshKeyCreationTest;
}
#endif // WITH_TESTS

} // namespace Remote::Internal

#ifdef WITH_TESTS
#include "sshkeycreation.moc"
#endif

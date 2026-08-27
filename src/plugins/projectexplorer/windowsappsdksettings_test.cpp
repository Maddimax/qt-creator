// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifdef WITH_TESTS

#include "windowsappsdksettings_test.h"

#include "windowsappsdksettings.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/temporarydirectory.h>

#include <QTest>

using namespace Utils;

namespace ProjectExplorer::Internal {

// What the page reports about the three paths. The Windows App SDK one is not
// "the directory is there" - it is "the SDK is in it" - and the download puts
// it somewhere other than where it was told to.
class WindowsAppSdkSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testAnSdkDirectoryIsOneWithAPackageInIt()
    {
        TemporaryDirectory dir("winappsdk");
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path().path());

        // An empty directory is not the SDK, however it is named.
        const FilePath named = root / "Microsoft.WindowsAppSDK.1.5";
        QVERIFY(named.ensureWritableDir());
        QVERIFY2(!hasWindowsAppSdkPackage(named),
                 "a directory named after the SDK was taken for the SDK");

        // A file that is not the package does not make it one either.
        QVERIFY((named / "readme.txt").writeFileContents("not a package"));
        QVERIFY(!hasWindowsAppSdkPackage(named));

        QVERIFY((named / "Microsoft.WindowsAppSDK.1.5.nupkg").writeFileContents("package"));
        QVERIFY(hasWindowsAppSdkPackage(named));

        // And a path that is not there at all answers no rather than asking
        // the filesystem something it cannot answer.
        QVERIFY(!hasWindowsAppSdkPackage(root / "nowhere"));
    }

    void testTheDownloadUnpacksIntoADirectoryOfItsOwn()
    {
        TemporaryDirectory dir("winappsdk-download");
        QVERIFY(dir.isValid());
        const FilePath downloadPath = FilePath::fromString(dir.path().path());

        // Nothing downloaded yet, so there is nowhere to point at - answered
        // as empty rather than as the download path itself, which would make
        // the summary report an SDK that is not there.
        QVERIFY2(windowsAppSdkPackageDir(downloadPath).isEmpty(),
                 qPrintable(windowsAppSdkPackageDir(downloadPath).toUserOutput()));

        // NuGet unpacks into a versioned directory, which is what the SDK
        // path has to end up being - not the directory it was downloaded to.
        const FilePath unpacked = downloadPath / "Microsoft.WindowsAppSDK.1.5.240311000";
        QVERIFY(unpacked.ensureWritableDir());
        QCOMPARE(windowsAppSdkPackageDir(downloadPath), unpacked);
        QVERIFY2(windowsAppSdkPackageDir(downloadPath) != downloadPath,
                 "the SDK path was left at the download path");

        // Something else in the same directory is not mistaken for it - not a
        // file beside it, and not another directory. "Extras" sorts before
        // "Microsoft...", so a search that takes the first directory it sees
        // rather than the first one that matches would answer with this.
        QVERIFY((downloadPath / "nuget.exe").writeFileContents("x"));
        QVERIFY((downloadPath / "Extras").ensureWritableDir());
        QCOMPARE(windowsAppSdkPackageDir(downloadPath), unpacked);
    }

    void testThePageDrawsItself()
    {
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&windowsAppSdkSettings(), "WindowsAppSdkPage.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        QVERIFY2(!windowsAppSdkSettings().isAutoApply(),
                 "the page auto-applies, so Apply would never save");

        // The summary is three checks, so the page can say which one failed
        // rather than only that something did.
        QCOMPARE(windowsAppSdkSettings().summary.aspects().size(), 3);
    }
};

QObject *createWindowsAppSdkSettingsTest()
{
    return new WindowsAppSdkSettingsTest;
}

} // namespace ProjectExplorer::Internal

#include "windowsappsdksettings_test.moc"

#endif // WITH_TESTS

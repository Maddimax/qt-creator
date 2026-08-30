// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcpkg_test.h"

#include "vcpkgmanifesteditor.h"
#include "vcpkgsearch.h"

#include <QTest>

namespace Vcpkg::Internal {

class VcpkgSearchTest : public QObject
{
    Q_OBJECT

private slots:
    void testVcpkgJsonParser_data();
    void testVcpkgJsonParser();
    void testAddDependency_data();
    void testAddDependency();

    void testWhichPackagesAFilterFinds();
    void testWhatTheDetailsPaneShows();
    void testWhenAPackageCanBeAdded();
    void testTheCMakeCodeAPackageNeeds();
};

using namespace Search;

static VcpkgManifest packageNamed(const QString &name, const QString &shortDescription = {},
                                  const QStringList &description = {})
{
    VcpkgManifest manifest;
    manifest.name = name;
    manifest.shortDescription = shortDescription;
    manifest.description = description;
    return manifest;
}

void VcpkgSearchTest::testTheCMakeCodeAPackageNeeds()
{
    // Two packages give two blocks, each of which is the three lines a
    // CMakeLists.txt needs, and they are separated so they can be pasted as
    // one lot.
    const QString code = cmakeCodeForPackages({"zlib", "fmt"});
    QVERIFY2(code.contains("zlib"), qPrintable(code));
    QVERIFY2(code.contains("fmt"), qPrintable(code));
    QVERIFY2(code.contains("find_package"), qPrintable(code));
    QVERIFY2(code.contains("target_link_libraries"), qPrintable(code));

    // The blocks are separated, not run together.
    QVERIFY2(code.contains("\n\n"), "the packages ran into one another");

    QVERIFY(cmakeCodeForPackages({}).isEmpty());
}

void VcpkgSearchTest::testWhichPackagesAFilterFinds()
{
    const QList<VcpkgManifest> packages = {
        packageNamed("zlib", "A compression library"),
        packageNamed("boost", "Peer-reviewed portable C++ libraries"),
        packageNamed("fmt", "Formatting", {"A modern formatting library"}),
    };

    // Nothing typed lists everything, in order - the widget list sorted the
    // names and the reader reads them that way.
    QCOMPARE(packageNamesMatching(packages, {}), (QStringList{"boost", "fmt", "zlib"}));

    // A package is found by its name...
    QCOMPARE(packageNamesMatching(packages, "zl"), (QStringList{"zlib"}));
    // ...case insensitively...
    QCOMPARE(packageNamesMatching(packages, "ZLIB"), (QStringList{"zlib"}));
    // ...by what it says it is...
    QCOMPARE(packageNamesMatching(packages, "compression"), (QStringList{"zlib"}));
    // ...and by a word from the longer description, which the list does not
    // even show.
    QCOMPARE(packageNamesMatching(packages, "modern"), (QStringList{"fmt"}));
    QCOMPARE(packageNamesMatching(packages, "A modern formatting library"),
             (QStringList{"fmt"}));

    QVERIFY(packageNamesMatching(packages, "nosuchpackage").isEmpty());
}

void VcpkgSearchTest::testWhatTheDetailsPaneShows()
{
    // The short description on its own when that is all there is.
    QCOMPARE(packageDescriptionHtml(packageNamed("zlib", "A compression library")),
             QString("A compression library"));

    // And each paragraph of the long one after it.
    QCOMPARE(packageDescriptionHtml(packageNamed("fmt", "Formatting", {"one", "two"})),
             QString("Formatting<p>one</p><p>two</p>"));

    QCOMPARE(packageDescriptionHtml({}), QString());
}

void VcpkgSearchTest::testWhenAPackageCanBeAdded()
{
    // Something has to be chosen.
    QVERIFY2(!canAddPackage({}, false), "a package could be added without choosing one");
    QVERIFY(canAddPackage("zlib", false));

    // And a package the project already depends on is not worth adding twice.
    QVERIFY2(!canAddPackage("zlib", true), "a package already depended on could be added again");
}

void VcpkgSearchTest::testVcpkgJsonParser_data()
{
    QTest::addColumn<QString>("vcpkgManifestJsonData");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("version");
    QTest::addColumn<QString>("license");
    QTest::addColumn<QStringList>("dependencies");
    QTest::addColumn<QString>("shortDescription");
    QTest::addColumn<QStringList>("description");
    QTest::addColumn<QUrl>("homepage");
    QTest::addColumn<bool>("success");

    QTest::newRow("cimg, version, short description")
            << R"({
                    "name": "cimg",
                    "version": "2.9.9",
                    "description": "The CImg Library is a small, open-source, and modern C++ toolkit for image processing",
                    "homepage": "https://github.com/dtschump/CImg",
                    "dependencies": [
                      "fmt",
                      {
                        "name": "vcpkg-cmake",
                        "host": true
                      }
                    ]
                   })"
            << "cimg"
            << "2.9.9"
            << ""
            << QStringList({"fmt", "vcpkg-cmake"})
            << "The CImg Library is a small, open-source, and modern C++ toolkit for image processing"
            << QStringList()
            << QUrl::fromUserInput("https://github.com/dtschump/CImg")
            << true;

    QTest::newRow("catch-classic, version-string, complete description")
            << R"({
                    "name": "catch-classic",
                    "version-string": "1.12.2",
                    "port-version": 1,
                    "description": [
                      "A modern, header-only test framework for unit tests",
                      "This is specifically the legacy 1.x branch provided for compatibility",
                      "with older compilers."
                    ],
                    "homepage": "https://github.com/catchorg/Catch2"
                  })"
            << "catch-classic"
            << "1.12.2"
            << ""
            << QStringList()
            << "A modern, header-only test framework for unit tests"
            << QStringList({"This is specifically the legacy 1.x branch provided for compatibility",
                            "with older compilers."})
            << QUrl::fromUserInput("https://github.com/catchorg/Catch2")
            << true;

    QTest::newRow("Incomplete")
            << R"({
                    "version-semver": "1.0",
                    "description": "foo",
                    "license": "WTFPL"
                  })"
            << ""
            << "1.0"
            << "WTFPL"
            << QStringList()
            << "foo"
            << QStringList()
            << QUrl()
            << false;
}

void VcpkgSearchTest::testVcpkgJsonParser()
{
    QFETCH(QString, vcpkgManifestJsonData);
    QFETCH(QString, name);
    QFETCH(QString, version);
    QFETCH(QString, license);
    QFETCH(QStringList, dependencies);
    QFETCH(QString, shortDescription);
    QFETCH(QStringList, description);
    QFETCH(QUrl, homepage);
    QFETCH(bool, success);

    bool ok = false;
    const Search::VcpkgManifest mf =
            Search::parseVcpkgManifest(vcpkgManifestJsonData.toUtf8(), &ok);

    QCOMPARE(mf.name, name);
    QCOMPARE(mf.version, version);
    QCOMPARE(mf.license, license);
    QCOMPARE(mf.dependencies, dependencies);
    QCOMPARE(mf.shortDescription, shortDescription);
    QCOMPARE(mf.description, description);
    QCOMPARE(mf.homepage, homepage);
    QCOMPARE(ok, success);
}

void VcpkgSearchTest::testAddDependency_data()
{
    QTest::addColumn<QString>("originalVcpkgManifestJsonData");
    QTest::addColumn<QString>("addedPackage");
    QTest::addColumn<QString>("modifiedVcpkgManifestJsonData");

    QTest::newRow("Existing dependencies")
        <<
R"({
    "name": "foo",
    "dependencies": [
        "fmt",
        {
            "name": "vcpkg-cmake",
            "host": true
        }
    ]
}
)"
        << "7zip"
        <<
R"({
    "dependencies": [
        "fmt",
        {
            "host": true,
            "name": "vcpkg-cmake"
        },
        "7zip"
    ],
    "name": "foo"
}
)";

    QTest::newRow("Without dependencies")
        <<
R"({
    "name": "foo"
}
)"
        << "7zip"
        <<
R"({
    "dependencies": [
        "7zip"
    ],
    "name": "foo"
}
)";
}

void VcpkgSearchTest::testAddDependency()
{
    QFETCH(QString, originalVcpkgManifestJsonData);
    QFETCH(QString, addedPackage);
    QFETCH(QString, modifiedVcpkgManifestJsonData);

    const QByteArray result = addDependencyToManifest(originalVcpkgManifestJsonData.toUtf8(),
                                                      addedPackage);

    QCOMPARE(QString::fromUtf8(result), modifiedVcpkgManifestJsonData);
}

QObject *createVcpkgSearchTest()
{
    return new VcpkgSearchTest;
}

} // namespace Vcpkg::Internal

#include "vcpkg_test.moc"

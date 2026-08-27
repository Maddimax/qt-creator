// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifdef WITH_TESTS

#include "cdbpaths_test.h"

#include "debuggeractions.h"
#include "shared/cdbsymbolpathlisteditor.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/temporarydirectory.h>

#include <QTest>

using namespace Utils;

namespace Debugger::Internal {

// A CDB symbol path is not a directory - it is "srv*<cache>*<url>" or
// "cache*<dir>" - so the two questions the page asks about one are where the
// cache directory inside it is, and how to spell a new one.
class CdbPathsTest : public QObject
{
    Q_OBJECT

private slots:
    void testTheCacheDirectoryComesOutOfTheSymbolPath()
    {
        const QString server
            = CdbSymbolPathListEditor::symbolPath(FilePath::fromUserInput("/tmp/cache"),
                                                  CdbSymbolPathListEditor::SymbolServerPath);
        // What the format actually is, so that the parsing below is not just
        // agreeing with itself.
        QVERIFY2(server.contains("/tmp/cache") && server.contains("http"), qPrintable(server));

        // The directory, not the path it was found in. Offering the whole
        // "srv*...*http://..." string as a place to write a cache into is what
        // this used to do.
        QCOMPARE(symbolCacheDirectory({server}), FilePath::fromUserInput("/tmp/cache"));

        const QString cache
            = CdbSymbolPathListEditor::symbolPath(FilePath::fromUserInput("/tmp/other"),
                                                  CdbSymbolPathListEditor::SymbolCachePath);
        QCOMPARE(symbolCacheDirectory({cache}), FilePath::fromUserInput("/tmp/other"));

        // A server entry is preferred over a cache entry, and an entry that
        // names no directory is not one to take a directory from.
        QCOMPARE(symbolCacheDirectory({cache, server}), FilePath::fromUserInput("/tmp/cache"));
        const QString bareServer
            = CdbSymbolPathListEditor::symbolPath({}, CdbSymbolPathListEditor::SymbolServerPath);
        QCOMPARE(symbolCacheDirectory({bareServer, cache}), FilePath::fromUserInput("/tmp/other"));

        // Nothing to go on falls back to a directory under the temporary one,
        // which is a directory rather than a symbol path.
        const FilePath fallback = symbolCacheDirectory({});
        QCOMPARE(fallback, TemporaryDirectory::masterDirectoryFilePath() / "symbolcache");
        QVERIFY2(!fallback.toUserOutput().contains('*'), qPrintable(fallback.toUserOutput()));
    }

    void testWhichEntryCarriesTheDirectory()
    {
        const FilePath dir = FilePath::fromUserInput("/tmp/cache");

        // With both, the cache entry names the directory and the server entry
        // does not: naming it twice makes CDB cache into it twice.
        const QStringList both = symbolPathsToAdd(true, true, dir);
        QCOMPARE(both.size(), 2);
        QVERIFY(CdbSymbolPathListEditor::isSymbolCachePath(both.at(0)));
        QVERIFY(CdbSymbolPathListEditor::isSymbolServerPath(both.at(1)));
        QVERIFY2(both.at(0).contains(dir.toUserOutput()), qPrintable(both.at(0)));
        QVERIFY2(!both.at(1).contains(dir.toUserOutput()), qPrintable(both.at(1)));

        // With only the server, it is the server entry that carries it -
        // otherwise the symbols are downloaded and never kept.
        const QStringList serverOnly = symbolPathsToAdd(false, true, dir);
        QCOMPARE(serverOnly.size(), 1);
        QVERIFY(CdbSymbolPathListEditor::isSymbolServerPath(serverOnly.first()));
        QVERIFY2(serverOnly.first().contains(dir.toUserOutput()), qPrintable(serverOnly.first()));

        const QStringList cacheOnly = symbolPathsToAdd(true, false, dir);
        QCOMPARE(cacheOnly, QStringList{
            CdbSymbolPathListEditor::symbolPath(dir, CdbSymbolPathListEditor::SymbolCachePath)});

        QVERIFY(symbolPathsToAdd(false, false, dir).isEmpty());
    }

    void testThePageDrawsItself()
    {
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings().page6, "CdbPathsPage.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        // A settings page that auto-applies never writes anything: the aspects
        // are registered after the container is built, and registerAspect()
        // turns auto-apply back on.
        QVERIFY2(!settings().page6.isAutoApply(),
                 "the CDB paths page auto-applies, so Apply would never save");
    }
};

QObject *createCdbPathsTest()
{
    return new CdbPathsTest;
}

} // namespace Debugger::Internal

#include "cdbpaths_test.moc"

#endif // WITH_TESTS

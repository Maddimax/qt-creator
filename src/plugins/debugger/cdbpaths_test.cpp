// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifdef WITH_TESTS

#include "cdbpaths_test.h"

#include "debuggeractions.h"
#include "cdb/cdboptionspage.h"
#include "shared/cdbsymbolpathlisteditor.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspectpresentation.h>
#include <utils/temporarydirectory.h>

#include <QAbstractItemModel>
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

    // The events are stored the way CDB spells them on its command line, and
    // an event that takes no filter never gets one.
    void testTheEventsRoundTripThroughTheirCdbSpelling()
    {
        CdbBreakEventsAspect aspect;
        QAbstractItemModel * const model = aspect.tableModel();
        QVERIFY(model);
        QCOMPARE(model->columnCount({}), 2);
        const int rows = model->rowCount({});
        QVERIFY2(rows > 0, "the model offers no events to break on");

        // Nothing ticked is no events, not a list of empty strings.
        aspect.setValue({});
        QVERIFY(aspect.value().isEmpty());

        // What the model holds is what the page will commit, and the aspect
        // reads it back rather than trusting the string it was given -
        // isDirty() is what asks, so it is what makes the two agree.
        const auto asShown = [&aspect] {
            aspect.isDirty();
            return aspect.volatileValue();
        };

        aspect.setValue({"eh", "out:Needle"});
        QCOMPARE(asShown(), QStringList({"eh", "out:Needle"}));

        // Which rows those are, read off the model rather than assumed.
        int checked = 0;
        int withFilter = 0;
        for (int row = 0; row < rows; ++row) {
            const QModelIndex event = model->index(row, 0);
            if (model->data(event, Qt::CheckStateRole).toInt() == Qt::Checked)
                ++checked;
            if (!model->data(model->index(row, 1), Qt::DisplayRole).toString().isEmpty())
                ++withFilter;
            // The event column is a check box and never a field.
            QVERIFY(model->data(event, Utils::AspectTable::CheckableRole).toBool());
        }
        QCOMPARE(checked, 2);
        QCOMPARE(withFilter, 1);

        // An event with no filter of its own cannot be given one, however the
        // stored string is spelled.
        aspect.setValue({"ct:nonsense"});
        QCOMPARE(asShown(), QStringList{"ct"});

        // And an event CDB does not know is dropped rather than carried
        // along, which is what the check states can express and a string
        // list cannot.
        // On its own, so that it cannot be mistaken for the event that is
        // ticked anyway: an unknown event has to leave nothing behind, not
        // stand in for whichever event happens to be first.
        aspect.setValue({"zz:something"});
        QVERIFY2(asShown().isEmpty(), qPrintable(asShown().join(", ")));
        aspect.setValue({"eh", "zz:something"});
        QCOMPARE(asShown(), QStringList{"eh"});

        // Typing into the view is refused for a row whose event takes no
        // filter, and the cell says so rather than only rejecting the write.
        aspect.setValue({});
        int withoutFilter = -1;
        int acceptsFilter = -1;
        for (int row = 0; row < rows; ++row) {
            const QModelIndex parameter = model->index(row, 1);
            if (model->data(parameter, Utils::AspectTable::EditableRole).toBool())
                acceptsFilter = row;
            else
                withoutFilter = row;
        }
        QVERIFY2(withoutFilter != -1 && acceptsFilter != -1,
                 "the events are all of one kind, so this proves nothing");

        QVERIFY2(!model->setData(model->index(withoutFilter, 1), "nonsense", Qt::EditRole),
                 "an event that takes no filter accepted one");
        QVERIFY(model->data(model->index(withoutFilter, 1), Qt::DisplayRole).toString().isEmpty());

        QVERIFY(model->setData(model->index(acceptsFilter, 1), "Needle", Qt::EditRole));
        QCOMPARE(model->data(model->index(acceptsFilter, 1), Qt::DisplayRole).toString(),
                 QString("Needle"));
    }

    void testThePageDrawsItself()
    {
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings().page6, "CdbPathsPage.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        const Utils::Result<> options
            = Core::aspectFormRenders(&settings().page5, "CdbOptionsPage.qml");
        QVERIFY2(options, qPrintable(options ? QString() : options.error()));
        QVERIFY2(!settings().page5.isAutoApply(),
                 "the CDB page auto-applies, so Apply would never save");

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

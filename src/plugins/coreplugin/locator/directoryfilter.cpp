// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "directoryfilter.h"

#include "locator.h"
#include "../coreplugintr.h"

#include "../dialogs/ioptionspage.h"

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/filesearch.h>
#include <utils/fileutils.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

using namespace Utils;

namespace Core {

/*!
    \class Core::DirectoryFilter
    \inmodule QtCreator
    \internal
*/

const char kDisplayNameKey[] = "displayName";
const char kDirectoriesKey[] = "directories";
const char kFiltersKey[] = "filters";
const char kFilesKey[] = "files";
const char kExclusionFiltersKey[] = "exclusionFilters";

const QStringList kFiltersDefault = {"*.h", "*.cpp", "*.ui", "*.qrc"};
const QStringList kExclusionFiltersDefault = {"*/.git/*", "*/.cvs/*", "*/.svn/*", "*/build/*"};

static QString defaultDisplayName()
{
    return Tr::tr("Generic Directory Filter");
}

static void refresh(QPromise<FilePaths> &promise, const FilePaths &directories,
                    const QStringList &filters, const QStringList &exclusionFilters,
                    const QString &displayName)
{
    SubDirFileContainer fileContainer(directories, filters, exclusionFilters);
    promise.setProgressRange(0, fileContainer.progressMaximum());
    FilePaths files;
    const auto end = fileContainer.end();
    for (auto it = fileContainer.begin(); it != end; ++it) {
        if (promise.isCanceled()) {
            promise.setProgressValueAndText(it.progressValue(),
                                            Tr::tr("%1 filter update: canceled").arg(displayName));
            return;
        }
        files << it->filePath;
        promise.setProgressValueAndText(it.progressValue(),
            Tr::tr("%1 filter update: %n files", nullptr, files.size()).arg(displayName));
    }
    promise.setProgressValue(fileContainer.progressMaximum());
    promise.addResult(files);
}

DirectoryFilter::DirectoryFilter(Id id)
    : m_filters(kFiltersDefault)
    , m_exclusionFilters(kExclusionFiltersDefault)
{
    setId(id);
    setDefaultIncludedByDefault(true);
    setDisplayName(defaultDisplayName());
    setDescription(Tr::tr("Locates files from a custom set of directories. Append \"+<number>\" "
                          "or \":<number>\" to jump to the given line number. Append another "
                          "\"+<number>\" or \":<number>\" to jump to the column number as well."));

    using namespace QtTaskTree;
    const auto groupSetup = [this] {
        if (!m_directories.isEmpty())
            return SetupResult::Continue; // Async task will run
        m_cache.setFilePaths({});
        return SetupResult::StopWithSuccess; // Group stops, skips async task
    };
    const auto onSetup = [this](Async<FilePaths> &async) {
        async.setConcurrentCallData(&refresh, m_directories, m_filters, m_exclusionFilters,
                                    displayName());
    };
    const auto onDone = [this](const Async<FilePaths> &async) {
        if (async.isResultAvailable())
            m_cache.setFilePaths(async.result());
    };
    const Group root {
        onGroupSetup(groupSetup),
        AsyncTask<FilePaths>(onSetup, onDone, CallDoneFlag::OnSuccess)
    };
    setRefreshRecipe(root);
}

void DirectoryFilter::saveState(QJsonObject &object) const
{
    if (displayName() != defaultDisplayName())
        object.insert(kDisplayNameKey, displayName());
    if (!m_directories.isEmpty()) {
        object.insert(kDirectoriesKey,
                      QJsonArray::fromStringList(
                          Utils::transform(m_directories, &FilePath::toUrlishString)));
    }
    if (m_filters != kFiltersDefault)
        object.insert(kFiltersKey, QJsonArray::fromStringList(m_filters));
    const std::optional<FilePaths> files = m_cache.filePaths();
    if (files) {
        object.insert(kFilesKey, QJsonArray::fromStringList(
                                     Utils::transform(*files, &FilePath::toUrlishString)));
    }
    if (m_exclusionFilters != kExclusionFiltersDefault)
        object.insert(kExclusionFiltersKey, QJsonArray::fromStringList(m_exclusionFilters));
}

static QStringList toStringList(const QJsonArray &array)
{
    return Utils::transform(array.toVariantList(), &QVariant::toString);
}

static FilePaths toFilePaths(const QJsonArray &array)
{
    return Utils::transform(array.toVariantList(),
                            [](const QVariant &v) { return FilePath::fromString(v.toString()); });
}

void DirectoryFilter::restoreState(const QJsonObject &object)
{
    setDisplayName(object.value(kDisplayNameKey).toString(defaultDisplayName()));
    m_directories = toFilePaths(object.value(kDirectoriesKey).toArray());
    m_filters = toStringList(
        object.value(kFiltersKey).toArray(QJsonArray::fromStringList(kFiltersDefault)));
    if (object.contains(kFilesKey)) {
        m_cache.setFilePaths(FilePaths::fromStrings(
            toStringList(object.value(kFilesKey).toArray())));
    }
    m_exclusionFilters = toStringList(
        object.value(kExclusionFiltersKey)
            .toArray(QJsonArray::fromStringList(kExclusionFiltersDefault)));
}

// Whether what the filter finds has to be worked out again. Only where it
// looks and what it looks for decide that: renaming the filter, or changing
// the prefix it answers to, leaves the same files found.
bool needsRefreshFor(const FilePaths &oldDirectories,
                     const QStringList &oldFilters,
                     const QStringList &oldExclusionFilters,
                     const FilePaths &directories,
                     const QStringList &filters,
                     const QStringList &exclusionFilters)
{
    return oldDirectories != directories || oldFilters != filters
           || oldExclusionFilters != exclusionFilters;
}

namespace Internal {

DirectoryFilterOptions::DirectoryFilterOptions(DirectoryFilter *filter,
                                               bool isCustomFilter,
                                               const FilePaths &directoriesValue,
                                               const QStringList &filtersValue,
                                               const QStringList &exclusionFiltersValue)
    : LocatorFilterOptions(filter)
{
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/locator/DirectoryFilterDialog.qml"));

    name.setQmlName("Name");
    name.setLabelText(Tr::tr("Name:"));
    name.setDisplayStyle(StringAspect::LineEditDisplay);
    name.setValue(filter->displayName());

    directories.setQmlName("Directories");
    directories.setLabelText(Tr::tr("Directories:"));
    directories.setDisplayStyle(StringListAspect::DisplayStyle::ListView);
    directories.setUiPathKind(AspectControls::PathKind::ExistingDirectory);
    directories.setUiNewEntryText({});
    directories.setValue(Utils::transform(directoriesValue, &FilePath::toUserOutput));

    // Only a filter the user made themselves has a name and directories of
    // their own; the built-in ones are given theirs by whoever registered
    // them and offer nothing but the patterns and the prefix.
    name.setVisible(isCustomFilter);
    directories.setVisible(isCustomFilter);

    filePattern.setQmlName("FilePattern");
    filePattern.setLabelText(Utils::msgFilePatternLabel());
    filePattern.setToolTip(Utils::msgFilePatternToolTip());
    filePattern.setDisplayStyle(StringAspect::LineEditDisplay);
    filePattern.setValue(Utils::transform(filtersValue, &QDir::toNativeSeparators).join(','));

    exclusionPattern.setQmlName("ExclusionPattern");
    exclusionPattern.setLabelText(Utils::msgExclusionPatternLabel());
    exclusionPattern.setToolTip(Utils::msgFilePatternToolTip(InclusionType::Excluded));
    exclusionPattern.setDisplayStyle(StringAspect::LineEditDisplay);
    exclusionPattern.setValue(
        Utils::transform(exclusionFiltersValue, &QDir::toNativeSeparators).join(','));
}

FilePaths DirectoryFilterOptions::chosenDirectories() const
{
    return Utils::transform(directories(), &FilePath::fromUserInput);
}

QStringList DirectoryFilterOptions::chosenFilters() const
{
    return Utils::splitFilterUiText(filePattern());
}

QStringList DirectoryFilterOptions::chosenExclusionFilters() const
{
    return Utils::splitFilterUiText(exclusionPattern());
}

} // namespace Internal

bool DirectoryFilter::openConfigDialog(QWidget *parent, bool &needsRefresh)
{
    Internal::DirectoryFilterOptions options(this, m_isCustomFilter, m_directories, m_filters,
                                   m_exclusionFilters);
    if (!ILocatorFilter::openConfigDialog(parent, &options))
        return false;

    const FilePaths directories = options.chosenDirectories();
    const QStringList filters = options.chosenFilters();
    const QStringList exclusionFilters = options.chosenExclusionFilters();
    needsRefresh = needsRefreshFor(m_directories, m_filters, m_exclusionFilters,
                                   directories, filters, exclusionFilters);

    setDisplayName(options.name().trimmed());
    m_directories = directories;
    m_filters = filters;
    m_exclusionFilters = exclusionFilters;
    return true;
}

void DirectoryFilter::setIsCustomFilter(bool value)
{
    m_isCustomFilter = value;
}

void DirectoryFilter::setDirectories(const FilePaths &directories)
{
    if (directories == m_directories)
        return;
    m_directories = directories;
    Internal::Locator::instance()->refresh({this});
}

void DirectoryFilter::addDirectory(const FilePath &directory)
{
    setDirectories(m_directories + FilePaths{directory});
}

void DirectoryFilter::removeDirectory(const FilePath &directory)
{
    FilePaths directories = m_directories;
    directories.removeOne(directory);
    setDirectories(directories);
}

void DirectoryFilter::setFilters(const QStringList &filters)
{
    m_filters = filters;
}

void DirectoryFilter::setExclusionFilters(const QStringList &exclusionFilters)
{
    m_exclusionFilters = exclusionFilters;
}

#ifdef WITH_TESTS

namespace Internal {

class DirectoryFilterTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        DirectoryFilter filter("Test.DirectoryFilter");
        DirectoryFilterOptions options(&filter, true, {}, kFiltersDefault,
                                       kExclusionFiltersDefault);
        const Result<> rendered
            = aspectFormRenders(&options, "locator/DirectoryFilterDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheDirectoriesArePickedNotTyped()
    {
        // The widget list's Add... and Edit... opened a folder chooser. A list
        // of strings that says nothing draws Add and no Edit at all, so the
        // aspect has to say what its entries are.
        DirectoryFilter filter("Test.DirectoryFilter");
        DirectoryFilterOptions options(&filter, true, {}, {}, {});
        QCOMPARE(options.directories.presentation().pathKind,
                 AspectControls::PathKind::ExistingDirectory);

        // And nothing is seeded into a new row: a path comes from the picker,
        // where a search URL comes from a template.
        QVERIFY2(options.directories.presentation().newEntryText.isEmpty(),
                 "Add puts something in the row that is not a directory");
    }

    void testThePatternsAreOneFieldOfMany()
    {
        DirectoryFilter filter("Test.DirectoryFilter");
        const DirectoryFilterOptions options(&filter, true, {},
                                             {"*.h", "*.cpp"}, {"*/build/*"});
        QCOMPARE(options.filePattern(), QString("*.h,*.cpp"));
        QCOMPARE(options.exclusionPattern(), QString("*/build/*"));

        QCOMPARE(options.chosenFilters(), (QStringList{"*.h", "*.cpp"}));
        QCOMPARE(options.chosenExclusionFilters(), (QStringList{"*/build/*"}));
    }

    void testWhitespaceAroundAPatternIsNotPartOfIt()
    {
        DirectoryFilter filter("Test.DirectoryFilter");
        DirectoryFilterOptions options(&filter, true, {}, {}, {});
        options.filePattern.setValue("*.h, *.cpp ");
        QCOMPARE(options.chosenFilters(), (QStringList{"*.h", "*.cpp"}));
    }

    void testOnlyAFilterOfTheReadersOwnHasDirectories()
    {
        // A filter someone else registered is given its directories by them;
        // the dialog shows the patterns and the prefix and nothing else.
        DirectoryFilter filter("Test.DirectoryFilter");
        const DirectoryFilterOptions builtIn(&filter, false, {}, {}, {});
        QVERIFY2(!builtIn.name.isVisible(), "a registered filter offers to be renamed");
        QVERIFY2(!builtIn.directories.isVisible(),
                 "a registered filter offers to be pointed somewhere else");

        const DirectoryFilterOptions custom(&filter, true, {}, {}, {});
        QVERIFY(custom.name.isVisible());
        QVERIFY(custom.directories.isVisible());
    }

    void testWhatMakesTheFilterLookAgain()
    {
        const FilePaths dirs = {FilePath::fromString("/one")};
        const QStringList filters = {"*.h"};
        const QStringList exclusions = {"*/build/*"};

        // Nothing that decides what is found has changed.
        QVERIFY(!needsRefreshFor(dirs, filters, exclusions, dirs, filters, exclusions));

        // Each of the three on its own does.
        QVERIFY(needsRefreshFor(dirs, filters, exclusions,
                                {FilePath::fromString("/two")}, filters, exclusions));
        QVERIFY(needsRefreshFor(dirs, filters, exclusions, dirs, {"*.cpp"}, exclusions));
        QVERIFY(needsRefreshFor(dirs, filters, exclusions, dirs, filters, {}));

        // Including one more directory, which is the usual edit.
        QVERIFY(needsRefreshFor(dirs, filters, exclusions,
                                dirs + FilePaths{FilePath::fromString("/two")},
                                filters, exclusions));
    }

    void testADirectoryKeepsItsPlaceThroughTheDialog()
    {
        // Round trip: the paths are shown as the reader wrote them and read
        // back as paths, in order.
        DirectoryFilter filter("Test.DirectoryFilter");
        const FilePaths given = {FilePath::fromString("/one"), FilePath::fromString("/two")};
        DirectoryFilterOptions options(&filter, true, given, {}, {});
        QCOMPARE(options.chosenDirectories(), given);

        options.directories.setValue(QStringList{"/two", "/one"});
        QCOMPARE(options.chosenDirectories(),
                 (FilePaths{FilePath::fromString("/two"), FilePath::fromString("/one")}));
    }
};

QObject *createDirectoryFilterTest()
{
    return new DirectoryFilterTest;
}

} // namespace Internal

#endif // WITH_TESTS

} // namespace Core

#include "directoryfilter.moc"

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "urllocatorfilter.h"

#include "../coreplugintr.h"

#include "../dialogs/ioptionspage.h"

#include <utils/algorithm.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonObject>

using namespace QtTaskTree;
using namespace Utils;

namespace Core {
namespace Internal {

// What a new entry starts as: a search URL with the placeholder in it, so
// that what has to be there is there to edit rather than to remember.
const char kNewUrlTemplate[] = "https://www.example.com/search?query=%1";

UrlFilterOptions::UrlFilterOptions(UrlLocatorFilter *filter)
    : LocatorFilterOptions(filter)
{
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/locator/UrlFilterDialog.qml"));

    name.setQmlName("Name");
    name.setLabelText(Tr::tr("Name:"));
    name.setDisplayStyle(StringAspect::LineEditDisplay);
    name.setValue(filter->displayName());
    // Only a filter the user made themselves has a name of their choosing;
    // the built-in ones are named by whoever registered them.
    name.setVisible(filter->isCustomFilter());

    urls.setQmlName("Urls");
    urls.setLabelText(Tr::tr("URLs:"));
    urls.setToolTip(Tr::tr("Add \"%1\" placeholder for the query string."));
    urls.setDisplayStyle(StringListAspect::DisplayStyle::ListView);
    urls.setUiAllowReordering(true);
    urls.setUiNewEntryText(newUrlTemplate());
    urls.setValue(filter->remoteUrls());
}

void UrlFilterOptions::applyTo(UrlLocatorFilter *filter) const
{
    LocatorFilterOptions::applyTo(filter);
    filter->setRemoteUrls(urls());
    if (filter->isCustomFilter())
        filter->setDisplayName(name());
}

QString UrlFilterOptions::newUrlTemplate()
{
    return QLatin1String(kNewUrlTemplate);
}

} // namespace Internal

// -- UrlLocatorFilter

/*!
    \class Core::UrlLocatorFilter
    \inmodule QtCreator
    \internal
*/

UrlLocatorFilter::UrlLocatorFilter(Id id)
    : UrlLocatorFilter(Tr::tr("URL Template"), id)
{}

UrlLocatorFilter::UrlLocatorFilter(const QString &displayName, Id id)
{
    setId(id);
    m_defaultDisplayName = displayName;
    setDisplayName(displayName);
}

LocatorMatcherTasks UrlLocatorFilter::matchers()
{
    const auto onSetup = [urls = remoteUrls()] {
        const LocatorStorage &storage = *LocatorStorage::storage();
        const QString input = storage.input();
        LocatorFilterEntries entries;
        for (const QString &url : urls) {
            const QString name = url.arg(input);
            LocatorFilterEntry entry;
            entry.displayName = name;
            entry.acceptor = [name] {
                if (!name.isEmpty())
                    QDesktopServices::openUrl(name);
                return AcceptResult();
            };
            entry.highlightInfo = {int(name.lastIndexOf(input)), int(input.size())};
            entries.append(entry);
        }
        storage.reportOutput(entries);
    };
    return {QSyncTask(onSetup)};
}

const char kDisplayNameKey[] = "displayName";
const char kRemoteUrlsKey[] = "remoteUrls";

void UrlLocatorFilter::saveState(QJsonObject &object) const
{
    if (displayName() != m_defaultDisplayName)
        object.insert(kDisplayNameKey, displayName());
    if (m_remoteUrls != m_defaultUrls)
        object.insert(kRemoteUrlsKey, QJsonArray::fromStringList(m_remoteUrls));
}

void UrlLocatorFilter::restoreState(const QJsonObject &object)
{
    setDisplayName(object.value(kDisplayNameKey).toString(m_defaultDisplayName));
    m_remoteUrls = Utils::transform(object.value(kRemoteUrlsKey)
                                        .toArray(QJsonArray::fromStringList(m_defaultUrls))
                                        .toVariantList(),
                                    &QVariant::toString);
}

bool UrlLocatorFilter::openConfigDialog(QWidget *parent, bool &needsRefresh)
{
    Q_UNUSED(needsRefresh)
    Internal::UrlFilterOptions options(this);
    if (!ILocatorFilter::openConfigDialog(parent, &options))
        return false;

    options.applyTo(this);
    return true;
}

void UrlLocatorFilter::addDefaultUrl(const QString &urlTemplate)
{
    m_remoteUrls.append(urlTemplate);
    m_defaultUrls.append(urlTemplate);
}

#ifdef WITH_TESTS

namespace Internal {

class UrlFilterTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        UrlLocatorFilter filter("Test.UrlFilter");
        UrlFilterOptions options(&filter);
        const Result<> rendered
            = Core::aspectFormRenders(&options, "locator/UrlFilterDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheDialogOpensOnWhatTheFilterHolds()
    {
        UrlLocatorFilter filter("Test.UrlFilter");
        filter.addDefaultUrl("https://one.example/?q=%1");
        filter.addDefaultUrl("https://two.example/?q=%1");
        filter.setShortcutString("u");

        const UrlFilterOptions options(&filter);
        QCOMPARE(options.urls(), filter.remoteUrls());
        QCOMPARE(options.shortcut(), QString("u"));
    }

    void testTheOrderOfTheUrlsIsTheReadersToSet()
    {
        // They are offered in the order they are listed, so the list has to
        // say so - without this it draws Add and Remove and nothing else.
        UrlLocatorFilter filter("Test.UrlFilter");
        UrlFilterOptions options(&filter);
        QVERIFY2(options.urls.presentation().allowReordering,
                 "the URLs can no longer be put in the order they are tried");

        options.urls.setValue({"first", "second"});
        options.applyTo(&filter);
        QCOMPARE(filter.remoteUrls(), (QStringList{"first", "second"}));

        options.urls.setValue({"second", "first"});
        options.applyTo(&filter);
        QCOMPARE(filter.remoteUrls(), (QStringList{"second", "first"}));
    }

    void testANewEntryStartsAsASearchUrl()
    {
        // The widget list put a template in the new row and started editing
        // it, so what has to be there is there to edit. An empty row would
        // mean remembering the placeholder.
        UrlLocatorFilter filter("Test.UrlFilter");
        const UrlFilterOptions options(&filter);
        const QString seeded = options.urls.presentation().newEntryText;
        QVERIFY2(!seeded.isEmpty(), "Add leaves the reader an empty row");
        QVERIFY2(seeded.contains("%1"), qPrintable(seeded));
        QCOMPARE(seeded, UrlFilterOptions::newUrlTemplate());
    }

    void testOnlyAFilterOfTheReadersOwnIsNamed()
    {
        // The built-in filters are named by whoever registered them, so the
        // field is not there - and a name typed into it must not reach them.
        UrlLocatorFilter builtIn("Bug Tracker", "Test.BuiltIn");
        UrlFilterOptions builtInOptions(&builtIn);
        QVERIFY2(!builtInOptions.name.isVisible(), "a built-in filter offers to be renamed");
        builtInOptions.name.setValue("Something Else");
        builtInOptions.applyTo(&builtIn);
        QCOMPARE(builtIn.displayName(), QString("Bug Tracker"));

        UrlLocatorFilter custom("Mine", "Test.Custom");
        custom.setIsCustomFilter(true);
        UrlFilterOptions customOptions(&custom);
        QVERIFY(customOptions.name.isVisible());
        customOptions.name.setValue("Renamed");
        customOptions.applyTo(&custom);
        QCOMPARE(custom.displayName(), QString("Renamed"));
    }
};

QObject *createUrlFilterTest()
{
    return new UrlFilterTest;
}

} // namespace Internal

#endif // WITH_TESTS

} // namespace Core

#include "urllocatorfilter.moc"

// Copyright (C) 2016 Dmitry Savchenko
// Copyright (C) 2016 Vasiliy Sorokin
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "settings.h"

#include "constants.h"
#include "keyword.h"
#include "lineparser.h"
#include "todoicons.h"
#include "todoitemsprovider.h"
#include "todooutputpane.h"
#include "todotr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/aspectwidgets.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcsettings.h>
#include <utils/shutdownguard.h>
#include <utils/theme/theme.h>

#ifdef WITH_TESTS
#include <QIcon>
#include <QTest>
#endif

using namespace Core;
using namespace Utils;

namespace Todo::Internal {

Settings &todoSettings()
{
    static Settings theTodoSettings;
    return theTodoSettings;
}

void Settings::save() const
{
    if (!keywordsEdited)
        return;

    QtcSettings *settings = ICore::settings();
    settings->beginGroup(Constants::SETTINGS_GROUP);
    settings->setValue(Constants::SCANNING_SCOPE, scanningScope);

    settings->beginWriteArray(Constants::KEYWORDS_LIST);
    if (const int size = keywords.size()) {
        const Key nameKey = "name";
        const Key colorKey = "color";
        const Key iconTypeKey = "iconType";
        for (int i = 0; i < size; ++i) {
            settings->setArrayIndex(i);
            settings->setValue(nameKey, keywords.at(i).name);
            settings->setValue(colorKey, keywords.at(i).color);
            settings->setValue(iconTypeKey, static_cast<int>(keywords.at(i).iconType));
        }
    }
    settings->endArray();

    settings->endGroup();
    settings->sync();
}

void Settings::load()
{
    setDefault();

    QtcSettings *settings = ICore::settings();
    settings->beginGroup(Constants::SETTINGS_GROUP);

    scanningScope = static_cast<ScanningScope>(settings->value(Constants::SCANNING_SCOPE,
        ScanningScopeCurrentFile).toInt());
    if (scanningScope >= ScanningScopeMax)
        scanningScope = ScanningScopeCurrentFile;

    KeywordList newKeywords;
    const int keywordsSize = settings->beginReadArray(Constants::KEYWORDS_LIST);
    if (keywordsSize > 0) {
        const Key nameKey = "name";
        const Key colorKey = "color";
        const Key iconTypeKey = "iconType";
        for (int i = 0; i < keywordsSize; ++i) {
            settings->setArrayIndex(i);
            Keyword keyword;
            keyword.name = settings->value(nameKey).toString();
            keyword.color = settings->value(colorKey).value<QColor>();
            keyword.iconType = static_cast<IconType>(settings->value(iconTypeKey).toInt());
            newKeywords << keyword;
        }
        keywords = newKeywords;
        keywordsEdited = true; // Otherwise they wouldn't have been saved
    }
    settings->endArray();

    settings->endGroup();
}

void Settings::setDefault()
{
    scanningScope = ScanningScopeCurrentFile;

    keywords.clear();

    Keyword keyword;

    keyword.name = "TODO";
    keyword.iconType = IconType::Todo;
    keyword.color = creatorColor(Utils::Theme::OutputPanes_NormalMessageTextColor);
    keywords.append(keyword);

    keyword.name = R"(\todo)";
    keyword.iconType = IconType::Todo;
    keyword.color = creatorColor(Utils::Theme::OutputPanes_NormalMessageTextColor);
    keywords.append(keyword);

    keyword.name = "NOTE";
    keyword.iconType = IconType::Info;
    keyword.color = creatorColor(Utils::Theme::OutputPanes_NormalMessageTextColor);
    keywords.append(keyword);

    keyword.name = "FIXME";
    keyword.iconType = IconType::Error;
    keyword.color = creatorColor(Utils::Theme::OutputPanes_ErrorMessageTextColor);
    keywords.append(keyword);

    keyword.name = "BUG";
    keyword.iconType = IconType::Bug;
    keyword.color = creatorColor(Utils::Theme::OutputPanes_ErrorMessageTextColor);
    keywords.append(keyword);

    keyword.name = "WARNING";
    keyword.iconType = IconType::Warning;
    keyword.color = creatorColor(Utils::Theme::OutputPanes_WarningMessageTextColor);
    keywords.append(keyword);

    keywordsEdited = false;
}

static bool operator==(const Settings &s1, const Settings &s2)
{
    return s1.keywords == s2.keywords
        && s1.scanningScope == s2.scanningScope
        && s1.keywordsEdited == s2.keywordsEdited;
}

// What the To-Do page edits. The settings themselves live in Settings, which
// the scanner and the output pane read and which keeps its own settings keys,
// so these aspects have none: they are read from it when the page is built and
// written back on apply.

class KeywordAspects final : public AspectContainer
{
public:
    KeywordAspects()
    {
        name.setQmlName("Name");
        name.setLabelText(Tr::tr("Keyword:"));
        name.setDisplayStyle(StringAspect::LineEditDisplay);
        // What LineParser::isKeywordSeparator() rejects, said once for the
        // field rather than only after OK was pressed.
        name.setValidationFunction([](const QString &text) -> Result<> {
            const QString trimmed = text.trimmed();
            if (trimmed.isEmpty())
                return ResultError(Tr::tr("The keyword cannot be empty."));
            for (const QChar c : trimmed) {
                if (LineParser::isKeywordSeparator(c)) {
                    return ResultError(Tr::tr("The keyword cannot contain spaces, colons, "
                                              "slashes or asterisks."));
                }
            }
            return ResultOk;
        });

        iconType.setQmlName("IconType");
        iconType.setLabelText(Tr::tr("Icon:"));
        iconType.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        // In IconType's order, so the index is the enumerator.
        iconType.addOption(Tr::tr("Information"));
        iconType.addOption(Tr::tr("Error"));
        iconType.addOption(Tr::tr("Warning"));
        iconType.addOption(Tr::tr("Bug"));
        iconType.addOption(Tr::tr("To-Do"));

        color.setQmlName("Color");
        color.setLabelText(Tr::tr("Color:"));
        color.setAlphaAllowed(false);

    }

    Keyword keyword() const
    {
        Keyword result;
        result.name = name.volatileValue().trimmed();
        result.iconType = IconType(iconType.volatileValue());
        result.color = color.volatileValue();
        return result;
    }

    void setKeyword(const Keyword &keyword)
    {
        name.setValue(keyword.name);
        iconType.setValue(int(keyword.iconType));
        color.setValue(keyword.color);
    }

    StringAspect name{this};
    SelectionAspect iconType{this};
    ColorAspect color{this};
};

class TodoAspects final : public AspectContainer
{
public:
    TodoAspects()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Todo/TodoSettingsPage.qml"));

        keywords.setQmlName("Keywords");
        keywords.setDisplayStyle(AspectList::DisplayStyle::ListViewWithDetails);
        keywords.setCreateItemFunction([] { return std::make_shared<KeywordAspects>(); });
        keywords.listViewDataCallback = [](KeywordAspects *item, int role) -> QVariant {
            const Keyword keyword = item->keyword();
            switch (role) {
            case Qt::DisplayRole:
                return keyword.name;
            // The list is how the keywords are told apart, so it shows each one
            // marked the way the code it marks will be.
            case Qt::DecorationRole:
                // Qualified: BaseAspect has an icon() of its own.
                return Todo::Internal::icon(keyword.iconType);
            case Qt::ForegroundRole:
                return keyword.color;
            }
            return {};
        };
        keywords.addExtraButton(Tr::tr("Reset"), [this] { setKeywords(defaultKeywords()); });

        scanningScope.setQmlName("ScanningScope");
        scanningScope.setLabelText(Tr::tr("Scanning Scope"));
        scanningScope.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
        // In ScanningScope's order, so the index is the enumerator.
        scanningScope.addOption(Tr::tr("Scan only the currently edited document"));
        scanningScope.addOption(Tr::tr("Scan the whole active project"));
        scanningScope.addOption(Tr::tr("Scan the current subproject"));

        readFromSettings();
    }

    void apply() override
    {
        AspectContainer::apply();

        Settings newSettings;
        newSettings.scanningScope = ScanningScope(scanningScope());
        for (const std::shared_ptr<BaseAspect> &item : keywords.items())
            newSettings.keywords << static_cast<KeywordAspects *>(item.get())->keyword();
        // "apply" itself is interpreted as "use these keywords, also for other
        // themes".
        newSettings.keywordsEdited = true;

        if (newSettings == todoSettings())
            return;

        todoSettings() = newSettings;
        todoSettings().save();

        todoItemsProvider().settingsChanged();
        todoOutputPane().setScanningScope(todoSettings().scanningScope);
    }

    void cancel() override
    {
        AspectContainer::cancel();
        readFromSettings();
    }

private:
    static KeywordList defaultKeywords()
    {
        Settings defaults;
        defaults.setDefault();
        return defaults.keywords;
    }

    void readFromSettings()
    {
        scanningScope.setValue(todoSettings().scanningScope);
        setKeywords(todoSettings().keywords);
        // What is stored is the page's starting point, not an edit of it: the
        // list marks items added since the last apply, and every row would be
        // shown as new otherwise. Reset does not do this, because there the
        // rows really are pending - the old ones struck through, the defaults
        // in bold, until Apply.
        keywords.apply();
    }

    void setKeywords(const KeywordList &list)
    {
        keywords.clear();
        for (const Keyword &keyword : list) {
            auto item = std::make_shared<KeywordAspects>();
            item->setKeyword(keyword);
            keywords.addItem(item);
        }
    }

    AspectList keywords{this};
    SelectionAspect scanningScope{this};
};

// TodoSettingsPage

class TodoSettingsPage final : public IOptionsPage
{
public:
    TodoSettingsPage()
    {
        setId(Constants::TODO_SETTINGS);
        setDisplayName(Tr::tr("To-Do"));
        setCategory("To-Do");
        setSettingsProvider([] {
            static GuardedObject<TodoAspects> theTodoAspects;
            return theTodoAspects.get();
        });
    }
};

void setupTodoSettingsPage()
{
    static TodoSettingsPage theTodoSettingsPage;

    QObject::connect(ICore::instance(), &Core::ICore::saveSettingsRequested,
                     [] { todoSettings().save(); });
}

#ifdef WITH_TESTS

// The page's keywords used to live in the QListWidget it built and in a modal
// dialog on top of it, so none of this could be checked without opening both.

class TodoSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testThePageShowsTheKeywordsThatAreStored();
    void testTheListSaysHowEachKeywordLooks();
    void testEditingAKeywordAndApplyingKeepsIt();
    void testCancelForgetsWhatWasTyped();
    void testResetOffersTheDefaultKeywordsBack();
    void testAKeywordCannotBeEmptyOrContainASeparator();

private:
    static KeywordAspects *itemAt(const AspectList &list, int index)
    {
        return static_cast<KeywordAspects *>(list.volatileItems().at(index).get());
    }

    static const AspectList &keywordsOf(const TodoAspects &page)
    {
        return static_cast<const AspectList &>(*page.aspects().first());
    }

    Settings m_original;
};

void TodoSettingsTest::init()
{
    m_original = todoSettings();

    Settings settings;
    Keyword keyword;
    keyword.name = "HACK";
    keyword.iconType = IconType::Bug;
    keyword.color = QColor(Qt::red);
    settings.keywords << keyword;
    keyword.name = "XXX";
    keyword.iconType = IconType::Warning;
    keyword.color = QColor(Qt::blue);
    settings.keywords << keyword;
    settings.scanningScope = ScanningScopeProject;
    todoSettings() = settings;
}

void TodoSettingsTest::cleanup()
{
    todoSettings() = m_original;
}

void TodoSettingsTest::testThePageShowsTheKeywordsThatAreStored()
{
    TodoAspects page;
    const AspectList &keywords = keywordsOf(page);

    QCOMPARE(keywords.volatileItems().size(), 2);
    QCOMPARE(itemAt(keywords, 0)->keyword().name, QString("HACK"));
    QCOMPARE(itemAt(keywords, 0)->keyword().iconType, IconType::Bug);
    QCOMPARE(itemAt(keywords, 1)->keyword().color, QColor(Qt::blue));

    // What is stored is where the page starts from, so there is nothing to
    // apply and no row is marked new.
    QVERIFY(!static_cast<const BaseAspect &>(page).isDirty());
    QCOMPARE(keywords.items().size(), 2);
}

void TodoSettingsTest::testTheListSaysHowEachKeywordLooks()
{
    TodoAspects page;
    const AspectList &keywords = keywordsOf(page);
    BaseAspect *first = keywords.volatileItems().first().get();

    QCOMPARE(keywords.listViewDataCallback(first, Qt::DisplayRole).toString(), QString("HACK"));
    QCOMPARE(keywords.listViewDataCallback(first, Qt::ForegroundRole).value<QColor>(),
             QColor(Qt::red));
    // The icon the keyword marks code with, not a name to look one up by.
    const QVariant decoration = keywords.listViewDataCallback(first, Qt::DecorationRole);
    QVERIFY(decoration.canConvert<QIcon>());
    QVERIFY(!decoration.value<QIcon>().isNull());
    QCOMPARE(decoration.value<QIcon>().cacheKey(), icon(IconType::Bug).cacheKey());
}

void TodoSettingsTest::testEditingAKeywordAndApplyingKeepsIt()
{
    TodoAspects page;
    const AspectList &keywords = keywordsOf(page);
    itemAt(keywords, 0)->name.setVolatileValue(QString("TODO"));
    QVERIFY(static_cast<const BaseAspect &>(page).isDirty());

    static_cast<BaseAspect &>(page).apply();

    QCOMPARE(todoSettings().keywords.size(), 2);
    QCOMPARE(todoSettings().keywords.at(0).name, QString("TODO"));
    QCOMPARE(todoSettings().keywords.at(0).iconType, IconType::Bug);
    QCOMPARE(todoSettings().keywords.at(0).color, QColor(Qt::red));
    QCOMPARE(todoSettings().scanningScope, ScanningScopeProject);
    // Applying is what says the keywords are the user's now, whatever theme
    // they were picked under.
    QVERIFY(todoSettings().keywordsEdited);
}

void TodoSettingsTest::testCancelForgetsWhatWasTyped()
{
    TodoAspects page;
    itemAt(keywordsOf(page), 0)->name.setVolatileValue(QString("TODO"));

    static_cast<BaseAspect &>(page).cancel();

    QCOMPARE(itemAt(keywordsOf(page), 0)->keyword().name, QString("HACK"));
    QCOMPARE(todoSettings().keywords.at(0).name, QString("HACK"));
}

void TodoSettingsTest::testResetOffersTheDefaultKeywordsBack()
{
    TodoAspects page;
    const AspectList &keywords = keywordsOf(page);
    QCOMPARE(keywords.extraButtonTexts(), QStringList{Tr::tr("Reset")});

    const_cast<AspectList &>(keywords).triggerExtraButton(0);

    Settings defaults;
    defaults.setDefault();
    QCOMPARE(keywords.volatileItems().size(), defaults.keywords.size());
    QCOMPARE(itemAt(keywords, 0)->keyword().name, defaults.keywords.at(0).name);

    // Offered, not done: the stored keywords are still the stored ones until
    // the page is applied.
    QCOMPARE(todoSettings().keywords.size(), 2);
    static_cast<BaseAspect &>(page).apply();
    QCOMPARE(todoSettings().keywords.size(), defaults.keywords.size());
}

void TodoSettingsTest::testAKeywordCannotBeEmptyOrContainASeparator()
{
    TodoAspects page;
    // Through the aspect, the way a control asks, rather than through the
    // function itself: what a form shows is what this answers.
    const BaseAspect &name = itemAt(keywordsOf(page), 0)->name;
    QCOMPARE(name.validationMessage("TODO"), QString());
    QVERIFY(!name.validationMessage("").isEmpty());
    QVERIFY(!name.validationMessage("   ").isEmpty());
    // What LineParser::isKeywordSeparator() rejects: the page used to say so
    // only after OK was pressed, in a red label of its own.
    QVERIFY(!name.validationMessage("TO DO").isEmpty());
    QVERIFY(!name.validationMessage("TODO:").isEmpty());
    QVERIFY(!name.validationMessage("TO/DO").isEmpty());
    QVERIFY(!name.validationMessage("TO*DO").isEmpty());
}

QObject *createTodoSettingsTest()
{
    return new TodoSettingsTest;
}

#endif // WITH_TESTS

} // Todo::Internal

#include "settings.moc"

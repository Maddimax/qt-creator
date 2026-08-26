// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codestyleaspect_test.h"

#include "codestyleeditor.h"
#include "codestylepool.h"
#include "icodestylepreferences.h"
#include "icodestylepreferencesfactory.h"
#include "tabsettings.h"
#include "textindenter.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspectwidgets.h>
#include <utils/layoutbuilder.h>

#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QWidget>

#include <memory>

using namespace Utils;

namespace TextEditor::Internal {

const char TEST_LANGUAGE_ID[] = "TextEditor.CodeStyleAspectTest";

// A minimal code style editor adopting the CodeStyleEditor dirty/changed
// contract: it edits a deferred TabSettings aspect and reports through it.
class TestCodeStyleEditor final : public CodeStyleEditor
{
public:
    explicit TestCodeStyleEditor(ICodeStylePreferences *codeStyle)
    {
        m_tabSettings.setAutoApply(false);
        m_tabSettings.setPreferences(codeStyle);
        connect(&m_tabSettings, &AspectContainer::volatileValueChanged,
                this, &CodeStyleEditor::changed);

        Layouting::Column{&m_tabSettings}.attachTo(this);
    }

    void apply() final { m_tabSettings.apply(); }
    void cancel() final { m_tabSettings.cancel(); }
    bool isDirty() const final { return m_tabSettings.isDirty(); }

private:
    TabSettings m_tabSettings;
};

class TestCodeStyleFactory final : public ICodeStylePreferencesFactory
{
public:
    TestCodeStyleFactory()
        : ICodeStylePreferencesFactory(TEST_LANGUAGE_ID)
    {
        setDisplayName(QString("Test"));
        setIndenterCreator([](QTextDocument *doc) { return new PlainTextIndenter(doc); });
        setCodeStyleCreator([] {
            auto prefs = new ICodeStylePreferences;
            prefs->setSettingsSuffix("TestCodeStyle");
            return prefs;
        });
        setValueEditorCreator([](ICodeStylePreferences *codeStyle) {
            return new TestCodeStyleEditor(codeStyle);
        });
    }
};

// A language that has a Qt Quick form, and one that has not. The Code Style
// page is per language, so the two have to be able to differ.
const char QML_TEST_LANGUAGE_ID[] = "TextEditor.CodeStyleAspectTest.Qml";

class QmlTestCodeStyleFactory final : public ICodeStylePreferencesFactory
{
public:
    QmlTestCodeStyleFactory()
        : ICodeStylePreferencesFactory(QML_TEST_LANGUAGE_ID)
    {
        setDisplayName(QString("Qml Test"));
        setIndenterCreator([](QTextDocument *doc) { return new PlainTextIndenter(doc); });
        setCodeStyleCreator([] {
            auto prefs = new ICodeStylePreferences;
            prefs->setSettingsSuffix("QmlTestCodeStyle");
            return prefs;
        });
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/CodeStyleTestPage.qml"));
        // A form names aspects, so the factory hands over the ones it edits.
        setSettingsAspectsCreator([](ICodeStylePreferences *codeStyle,
                                     CodeStylePreviewAspect *) {
            auto settings = new Utils::AspectContainer;
            auto lineLength = new Utils::IntegerAspect(settings);
            lineLength->setQmlName("LineLength");
            lineLength->setRange(0, 999);
            lineLength->setValue(80);

            // A real factory's aspects edit the page's *copy* of the style as
            // they are changed - that is what the copy is for, and why they
            // auto-apply. Without one of those here, nothing this page does
            // could ever be worth applying.
            auto tabSize = new Utils::IntegerAspect(settings);
            tabSize->setQmlName("TabSize");
            tabSize->setRange(1, 32);
            tabSize->setValue(codeStyle->tabSettings().m_tabSize);
            QObject::connect(tabSize, &Utils::BaseAspect::changed, tabSize,
                             [codeStyle, tabSize] {
                                 TabSettingsData settings = codeStyle->tabSettings();
                                 settings.m_tabSize = tabSize->value();
                                 codeStyle->setTabSettings(settings);
                             });
            return settings;
        });
    }
};

const char LIVE_TEST_LANGUAGE_ID[] = "TextEditor.CodeStyleAspectTest.Live";

// A code style editor that writes edits straight through to its preferences
// (like the C++/QML editors), reporting them via changed() and leaving
// apply/cancel/isDirty to the hosting CodeStyleAspect and its page-local copy.
class LiveTestCodeStyleEditor final : public CodeStyleEditor
{
public:
    explicit LiveTestCodeStyleEditor(ICodeStylePreferences *codeStyle)
    {
        m_tabSettings.setPreferences(codeStyle);
        connect(codeStyle, &ICodeStylePreferences::currentTabSettingsChanged,
                this, &CodeStyleEditor::changed);
        connect(codeStyle, &ICodeStylePreferences::currentValueChanged,
                this, &CodeStyleEditor::changed);

        Layouting::Column{&m_tabSettings}.attachTo(this);
    }

private:
    TabSettings m_tabSettings;
};

class LiveTestCodeStyleFactory final : public ICodeStylePreferencesFactory
{
public:
    LiveTestCodeStyleFactory()
        : ICodeStylePreferencesFactory(LIVE_TEST_LANGUAGE_ID)
    {
        setDisplayName(QString("Live Test"));
        setIndenterCreator([](QTextDocument *doc) { return new PlainTextIndenter(doc); });
        setCodeStyleCreator([] {
            auto prefs = new ICodeStylePreferences;
            prefs->setSettingsSuffix("LiveTestCodeStyle");
            return prefs;
        });
        setValueEditorCreator([](ICodeStylePreferences *codeStyle) {
            return new LiveTestCodeStyleEditor(codeStyle);
        });
    }
};

const char MODEL_TEST_LANGUAGE_ID[] = "TextEditor.CodeStyleAspectTest.Model";

// Exercises ICodeStylePreferencesFactory::setupCodeStyles(): a built-in style
// plus an editable global that delegates to it.
class ModelTestCodeStyleFactory final : public ICodeStylePreferencesFactory
{
public:
    ModelTestCodeStyleFactory()
        : ICodeStylePreferencesFactory(MODEL_TEST_LANGUAGE_ID)
    {
        setCodeStyleCreator([] {
            auto prefs = new ICodeStylePreferences;
            prefs->setSettingsSuffix("ModelTestCodeStyle");
            return prefs;
        });
        setGlobalCodeStyleId("ModelTestGlobal");
        setDefaultCodeStyleId("builtin");
        setBuiltInCodeStyles([this](CodeStylePool *pool) {
            m_builtin.setId("builtin");
            m_builtin.setReadOnly(true);
            TabSettingsData ts;
            ts.m_tabSize = 3;
            ts.m_indentSize = 3;
            m_builtin.setTabSettings(ts);
            pool->addCodeStyle(&m_builtin);
        });
        setupCodeStyles();
    }

private:
    ICodeStylePreferences m_builtin;
};

const char VALUE_TEST_LANGUAGE_ID[] = "TextEditor.CodeStyleAspectTest.Value";

// A value editor (the option C style): just the widgets editing the
// preferences live. The hosting CodeStyleAspect builds the selector and preview
// and owns the deferral and dirtiness, so the value editor has no
// apply/cancel/isDirty of its own.
class ValueTestCodeStyleWidget final : public QWidget
{
public:
    explicit ValueTestCodeStyleWidget(ICodeStylePreferences *codeStyle)
    {
        m_tabSettings.setPreferences(codeStyle);
        Layouting::Column{&m_tabSettings}.attachTo(this);
    }

private:
    TabSettings m_tabSettings;
};

class ValueTestCodeStyleFactory final : public ICodeStylePreferencesFactory
{
public:
    ValueTestCodeStyleFactory()
        : ICodeStylePreferencesFactory(VALUE_TEST_LANGUAGE_ID)
    {
        setDisplayName(QString("Value Test"));
        setIndenterCreator([](QTextDocument *doc) { return new PlainTextIndenter(doc); });
        setCodeStyleCreator([] {
            auto prefs = new ICodeStylePreferences;
            prefs->setSettingsSuffix("ValueTestCodeStyle");
            return prefs;
        });
        setValueEditorCreator([](ICodeStylePreferences *codeStyle) {
            return new ValueTestCodeStyleWidget(codeStyle);
        });
    }
};

const char QML_POOL_TEST_LANGUAGE_ID[] = "TextEditor.CodeStyleAspectTest.QmlPool";

// A Qt Quick language with a pool behind it, so that there is more than one
// style to delegate to and the page's selector has something to offer.
class QmlPoolTestCodeStyleFactory final : public ICodeStylePreferencesFactory
{
public:
    QmlPoolTestCodeStyleFactory()
        : ICodeStylePreferencesFactory(QML_POOL_TEST_LANGUAGE_ID)
    {
        setDisplayName(QString("Qml Pool Test"));
        setPreviewText(QString("if (a) {\nb;\n}\n"));
        setIndenterCreator([](QTextDocument *doc) { return new PlainTextIndenter(doc); });
        setCodeStyleCreator([] {
            auto prefs = new ICodeStylePreferences;
            prefs->setSettingsSuffix("QmlPoolTestCodeStyle");
            return prefs;
        });
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/CodeStyleTestPage.qml"));
        // A language whose formatting is more than its indenter says so, and
        // gets a Format button that runs this. Shouting stands in for
        // qmlformat, which a test must not depend on having.
        setPreviewFormatter([](ICodeStylePreferences *, const QString &text) {
            return Utils::Result<QString>(text.toUpper());
        });
        setGlobalCodeStyleId("QmlPoolTestGlobal");
        setDefaultCodeStyleId("narrow");
        setBuiltInCodeStyles([this](CodeStylePool *pool) {
            m_narrow.setId("narrow");
            m_narrow.setDisplayName(QString("Narrow"));
            m_narrow.setReadOnly(true);
            TabSettingsData narrow;
            narrow.m_tabSize = 2;
            narrow.m_indentSize = 2;
            m_narrow.setTabSettings(narrow);
            pool->addCodeStyle(&m_narrow);

            m_wide.setId("wide");
            m_wide.setDisplayName(QString("Wide"));
            m_wide.setReadOnly(true);
            TabSettingsData wide;
            wide.m_tabSize = 8;
            wide.m_indentSize = 8;
            m_wide.setTabSettings(wide);
            pool->addCodeStyle(&m_wide);
        });
        setupCodeStyles();
    }

private:
    ICodeStylePreferences m_narrow;
    ICodeStylePreferences m_wide;
};

class CodeStyleAspectTest final : public QObject
{
    Q_OBJECT

private:
    // The page's aspects are reached by the name its QML uses.
    static BaseAspect *aspectNamed(const AspectContainer *container, const QString &qmlName)
    {
        const QList<BaseAspect *> aspects = container->aspects();
        for (BaseAspect *aspect : aspects) {
            if (aspect->qmlName() == qmlName)
                return aspect;
        }
        return nullptr;
    }

    static int indexOfStyleNamed(const SelectionAspect *style, const QString &displayName)
    {
        for (int i = 0, n = style->optionCount(); i < n; ++i) {
            if (style->displayForIndex(i).contains(displayName))
                return i;
        }
        return -1;
    }

    // Locates the tab-size spin box by its (distinctive) current value.
    static QSpinBox *spinBoxWithValue(const QWidget *root, int value)
    {
        const QList<QSpinBox *> boxes = root->findChildren<QSpinBox *>();
        for (QSpinBox *box : boxes) {
            if (box->value() == value)
                return box;
        }
        return nullptr;
    }

    static TabSettingsData makeTabSettings(int tabSize, int indentSize)
    {
        TabSettingsData data;
        data.m_tabSize = tabSize;
        data.m_indentSize = indentSize;
        return data;
    }

private slots:
    void testALanguageMovesToQuickOnItsOwn()
    {
        // A language that names no form keeps its widget editor, so the page
        // declines it and nothing changes for the others.
        {
            TestCodeStyleFactory plain;
            ICodeStylePreferences codeStyle;
            CodeStyleAspect aspect(&codeStyle, TEST_LANGUAGE_ID);
            QVERIFY(aspect.qmlSource().isEmpty());
        }

        // One that does is rendered from it. Which language it is decides,
        // because the page is per language.
        {
            QmlTestCodeStyleFactory quick;
            ICodeStylePreferences codeStyle;
            CodeStyleAspect aspect(&codeStyle, QML_TEST_LANGUAGE_ID);
            QCOMPARE(aspect.qmlSource(),
                     QUrl("qrc:/qt/qml/QtCreator/TextEditor/CodeStyleTestPage.qml"));

            // And the aspects its form names are on the page, since the page
            // knows nothing about any language's settings itself.
            Utils::AspectContainer *settings = nullptr;
            const QList<BaseAspect *> aspects = aspect.aspects();
            for (BaseAspect *child : aspects) {
                if (auto container = qobject_cast<Utils::AspectContainer *>(child))
                    settings = container;
            }
            QVERIFY(settings);
            QCOMPARE(settings->aspects().size(), 2);
            QCOMPARE(settings->aspects().first()->qmlName(), QString("LineLength"));
        }

        // A language with no form contributes no aspects either: nothing is
        // built for a page that is not going to show it.
        {
            TestCodeStyleFactory plain;
            ICodeStylePreferences codeStyle;
            CodeStyleAspect aspect(&codeStyle, TEST_LANGUAGE_ID);
            QVERIFY(aspect.aspects().isEmpty());
        }
    }

    void testUnopenedPageIsNotDirty()
    {
        TestCodeStyleFactory factory;
        ICodeStylePreferences codeStyle;
        codeStyle.setTabSettings(makeTabSettings(11, 7));

        CodeStyleAspect aspect(&codeStyle, TEST_LANGUAGE_ID);
        // The layouter was never run, so nothing was edited.
        QVERIFY(!aspect.isDirty());
    }

    void testEditMakesDirtyAndApplyCommits()
    {
        TestCodeStyleFactory factory;
        ICodeStylePreferences codeStyle;
        codeStyle.setTabSettings(makeTabSettings(11, 7));

        CodeStyleAspect aspect(&codeStyle, TEST_LANGUAGE_ID);
        QWidget host;
        Utils::AspectWidgets::layouter(&aspect)().attachTo(&host);

        QVERIFY(!aspect.isDirty());

        QSpinBox *tabSize = spinBoxWithValue(&host, 11);
        QVERIFY(tabSize);
        tabSize->setValue(13);

        QVERIFY(aspect.isDirty());
        // The edit is held in the aspect, not yet written to the real style.
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 11);

        aspect.apply();
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 13);
        QVERIFY(!aspect.isDirty());
    }

    void testTheApplyButtonReachesTheStyleAndNotJustTheAspect()
    {
        // The test above calls aspect.apply() itself. The Preferences dialog
        // does not - it presses IOptionsPageWidget::apply(), which decides for
        // itself whether the page has anything to commit. It used to decide by
        // reading the first aspect's isAutoApply(), see the live-editing child
        // this page keeps on purpose, soft-assert and return; apply() never
        // ran and a code style change was never saved. The aspect was tested
        // and the button was not, so nothing said so.
        // The Quick factory, because that is the shape the bug needed: a
        // language that names a form gets its settings registered as child
        // aspects, and those edit the page's copy live. A widget-path page
        // registers none, so the check never looked at anything.
        QmlTestCodeStyleFactory factory;
        ICodeStylePreferences codeStyle;
        codeStyle.setTabSettings(makeTabSettings(11, 7));

        CodeStyleAspect aspect(&codeStyle, QML_TEST_LANGUAGE_ID);
        QVERIFY2(!aspect.aspects().isEmpty(), "no child aspects, so this proves nothing");
        QVERIFY2(aspect.aspects().first()->isAutoApply(),
                 "the first child does not auto-apply, so this is not the case that broke");

        Utils::AspectContainer *settings = nullptr;
        for (BaseAspect *child : aspect.aspects()) {
            if (auto container = qobject_cast<Utils::AspectContainer *>(child))
                settings = container;
        }

        // Built the way the dialog builds one. Not registered globally: this is
        // a page of its own, not one every other test then has to walk past.
        class Page : public Core::IOptionsPage
        {
        public:
            explicit Page(CodeStyleAspect *aspect)
                : Core::IOptionsPage(false)
            {
                setSettingsProvider([aspect] { return aspect; });
            }
        };

        Page page(&aspect);
        std::unique_ptr<Core::IOptionsPageWidget> widget(page.createWidget());
        QVERIFY(widget);

        // The page names a form, so it has to have rendered one. Apply below
        // works off the container and would commit either way, so without this
        // a page whose QML never loaded reads exactly like one that works.
        QWidget *quick = nullptr;
        for (QWidget *child : widget->findChildren<QWidget *>()) {
            if (qstrcmp(child->metaObject()->className(), "QQuickWidget") == 0)
                quick = child;
        }
        QVERIFY2(quick, "the page built no Qt Quick form");
        // By value because TextEditor does not link Qt Quick Widgets; asked
        // through the property system for the same reason.
        constexpr int quickWidgetReady = 1; // QQuickWidget::Ready
        QCOMPARE(quick->property("status").toInt(), quickWidgetReady);

        // Edited through the page's own settings aspects rather than a widget:
        // a Quick page has no spin box to find.
        QVERIFY(settings);
        Utils::IntegerAspect *tabSize = nullptr;
        for (BaseAspect *child : settings->aspects()) {
            if (child->qmlName() == "TabSize")
                tabSize = qobject_cast<Utils::IntegerAspect *>(child);
        }
        QVERIFY2(tabSize, "the test form does not offer a tab size to change");
        tabSize->setValue(13);

        // Held in the page's own copy until the button is pressed.
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 11);

        widget->apply();
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 13);
    }

    void testCancelReverts()
    {
        TestCodeStyleFactory factory;
        ICodeStylePreferences codeStyle;
        codeStyle.setTabSettings(makeTabSettings(11, 7));

        CodeStyleAspect aspect(&codeStyle, TEST_LANGUAGE_ID);
        QWidget host;
        Utils::AspectWidgets::layouter(&aspect)().attachTo(&host);

        QSpinBox *tabSize = spinBoxWithValue(&host, 11);
        QVERIFY(tabSize);
        tabSize->setValue(13);
        QVERIFY(aspect.isDirty());

        aspect.cancel();
        QVERIFY(!aspect.isDirty());
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 11);
        QCOMPARE(tabSize->value(), 11);
    }

    // Same expectations, but with an editor that writes through immediately
    // (the C++/QML style): the CodeStyleAspect page-local copy provides the
    // deferral and the comparison-based dirtiness.
    void testLiveWriteEditor()
    {
        LiveTestCodeStyleFactory factory;
        ICodeStylePreferences codeStyle;
        codeStyle.setTabSettings(makeTabSettings(11, 7));

        CodeStyleAspect aspect(&codeStyle, LIVE_TEST_LANGUAGE_ID);
        QWidget host;
        Utils::AspectWidgets::layouter(&aspect)().attachTo(&host);

        QVERIFY(!aspect.isDirty());

        QSpinBox *tabSize = spinBoxWithValue(&host, 11);
        QVERIFY(tabSize);
        tabSize->setValue(13);

        QVERIFY(aspect.isDirty());
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 11);

        aspect.apply();
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 13);
        QVERIFY(!aspect.isDirty());

        tabSize->setValue(17);
        QVERIFY(aspect.isDirty());
        aspect.cancel();
        QVERIFY(!aspect.isDirty());
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 13);
    }

    // The option C path: the language supplies only a value editor, and the
    // CodeStyleAspect builds the selector and preview around it. Behaves like
    // the live-write editor: edits mark dirty, apply commits, cancel reverts.
    void testValueEditor()
    {
        ValueTestCodeStyleFactory factory;
        ICodeStylePreferences codeStyle;
        codeStyle.setTabSettings(makeTabSettings(11, 7));

        CodeStyleAspect aspect(&codeStyle, VALUE_TEST_LANGUAGE_ID);
        QWidget host;
        Utils::AspectWidgets::layouter(&aspect)().attachTo(&host);

        QVERIFY(!aspect.isDirty());

        QSpinBox *tabSize = spinBoxWithValue(&host, 11);
        QVERIFY(tabSize);
        tabSize->setValue(13);

        QVERIFY(aspect.isDirty());
        // The edit went into the page-local copy, not the real style.
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 11);

        aspect.apply();
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 13);
        QVERIFY(!aspect.isDirty());

        tabSize->setValue(17);
        QVERIFY(aspect.isDirty());
        aspect.cancel();
        QVERIFY(!aspect.isDirty());
        QCOMPARE(codeStyle.tabSettings().m_tabSize, 13);
    }

    // A live-write editor while a (custom, editable) pool delegate is active.
    // The page edits a page-local copy of the delegate, so the shared pool
    // delegate stays untouched until apply(); cancel() must revert the edit.
    void testDelegateCancelReverts()
    {
        LiveTestCodeStyleFactory factory;
        CodeStylePool pool(&factory, LIVE_TEST_LANGUAGE_ID);
        pool.setTransient(true); // do not persist the test style to the settings
        ICodeStylePreferences *delegate = pool.createCodeStyle("Custom");
        const TabSettingsData original = makeTabSettings(5, 8);
        delegate->setTabSettings(original);

        ICodeStylePreferences codeStyle;
        codeStyle.setId("RealStyle");
        codeStyle.setDelegatingPool(&pool);
        codeStyle.setTabSettings(makeTabSettings(11, 7));
        codeStyle.setCurrentDelegate(delegate);

        CodeStyleAspect aspect(&codeStyle, LIVE_TEST_LANGUAGE_ID);
        QWidget host;
        Utils::AspectWidgets::layouter(&aspect)().attachTo(&host);

        // The editor shows the active delegate's tab size (5).
        QSpinBox *tabSize = spinBoxWithValue(&host, 5);
        QVERIFY(tabSize);
        tabSize->setValue(9);

        // The edit is held by the page copy and not committed to the shared
        // pool delegate.
        QCOMPARE(delegate->tabSettings(), original);
        QVERIFY(aspect.isDirty());

        aspect.cancel();
        QVERIFY(!aspect.isDirty());
        QCOMPARE(delegate->tabSettings(), original);
    }

    // The selector is the page's, not the language's: every Qt Quick Code Style
    // page gets the same one, over the styles its own pool holds.
    void testAQuickPageOffersTheStylesToDelegateTo()
    {
        QmlPoolTestCodeStyleFactory factory;
        CodeStyleAspect aspect(factory.globalCodeStyle(), QML_POOL_TEST_LANGUAGE_ID);

        auto style = qobject_cast<SelectionAspect *>(aspectNamed(&aspect, "Style"));
        QVERIFY(style);
        QCOMPARE(style->optionCount(), 2);

        // A style reads with what it is, so that a read-only one is recognisable
        // before picking it.
        const int narrow = indexOfStyleNamed(style, "Narrow");
        const int wide = indexOfStyleNamed(style, "Wide");
        QVERIFY(narrow >= 0);
        QVERIFY(wide >= 0);
        QVERIFY(style->displayForIndex(narrow).contains("built-in"));

        // And it starts on whichever one the style actually delegates to.
        QCOMPARE(style->value(), narrow);

        // The rest of the selector is there too, and knows what may be done to
        // a built-in: nothing.
        QVERIFY(aspectNamed(&aspect, "CopyStyle"));
        QVERIFY(aspectNamed(&aspect, "ImportStyle"));
        QVERIFY(aspectNamed(&aspect, "ExportStyle"));
        BaseAspect *remove = aspectNamed(&aspect, "RemoveStyle");
        QVERIFY(remove);
        QVERIFY(!remove->isEnabled());
        BaseAspect *note = aspectNamed(&aspect, "ReadOnlyNote");
        QVERIFY(note);
        QVERIFY(note->isVisible());
    }

    void testPickingAStyleIsAnEditThatCancelReverts()
    {
        QmlPoolTestCodeStyleFactory factory;
        ICodeStylePreferences *global = factory.globalCodeStyle();
        const QByteArray originalDelegate = global->currentDelegateId();

        CodeStyleAspect aspect(global, QML_POOL_TEST_LANGUAGE_ID);
        auto style = qobject_cast<SelectionAspect *>(aspectNamed(&aspect, "Style"));
        QVERIFY(style);
        QVERIFY(!aspect.isDirty());

        // What the form writes when the user picks from the combo box.
        const int wide = indexOfStyleNamed(style, "Wide");
        QVERIFY(wide >= 0);
        style->setVolatileValue(wide);

        // The page is now editing a different style, and says so - without
        // having touched the real one.
        QVERIFY(aspect.isDirty());
        QCOMPARE(global->currentDelegateId(), originalDelegate);

        aspect.cancel();
        QVERIFY(!aspect.isDirty());
        // And the selector went back with it, rather than staying on a style
        // the page is no longer editing.
        QCOMPARE(style->value(), indexOfStyleNamed(style, "Narrow"));

        style->setVolatileValue(wide);
        aspect.apply();
        QCOMPARE(global->currentDelegateId(), QByteArray("wide"));
        QVERIFY(!aspect.isDirty());
    }

    // The preview is a view of what the page is editing, not of what is saved.
    void testAQuickPageBringsItsOwnPreview()
    {
        QmlPoolTestCodeStyleFactory factory;
        ICodeStylePreferences *global = factory.globalCodeStyle();
        CodeStyleAspect aspect(global, QML_POOL_TEST_LANGUAGE_ID);

        auto preview = qobject_cast<CodeStylePreviewAspect *>(aspectNamed(&aspect, "Preview"));
        QVERIFY(preview);
        QCOMPARE(preview->value(), factory.previewText());
        QCOMPARE(preview->languageIdString(), QString(QML_POOL_TEST_LANGUAGE_ID));

        // The page's own copy of the preferences, so that the preview shows the
        // edits being made. Showing the saved style would look almost right.
        QVERIFY(preview->codeStyleObject());
        QVERIFY(preview->codeStyleObject() != global);

        preview->setValue(QString("something else"));
        preview->resetText();
        QCOMPARE(preview->value(), factory.previewText());
    }

    // The Format button runs the language's own formatter over the preview,
    // because for QML/JS the formatting is qmlformat and not the indenter.
    // The other place a code style preview is drawn: a project's Code Style
    // panel. Project panels are not options pages, so the census never walks
    // one and CodeStyleProjectForm.qml had never been rendered by anything.
    void testTheProjectFormNamesAspectsThatExist()
    {
        // A code style of its own, not the factory's global one: the selector
        // this form builds writes to whatever it is given, and a test that
        // hands it shared state leaves that state changed for the next one.
        QmlTestCodeStyleFactory factory;
        ICodeStylePreferences codeStyle;
        const std::unique_ptr<Utils::AspectContainer> aspects(
            factory.createProjectAspects(Utils::FilePath(), &codeStyle));
        QVERIFY(aspects);
        QVERIFY2(!aspects->qmlSource().isEmpty(), "the project form names no QML");

        static QStringList messages;
        messages.clear();
        QtMessageHandler previous = qInstallMessageHandler(
            [](QtMsgType, const QMessageLogContext &, const QString &text) {
                messages.append(text);
            });
        const std::unique_ptr<QWidget> form(Core::createAspectForm(aspects.get()));
        qInstallMessageHandler(previous);

        QVERIFY(form);
        QWidget *quick = nullptr;
        for (QWidget *child : form->findChildren<QWidget *>()) {
            if (qstrcmp(child->metaObject()->className(), "QQuickWidget") == 0)
                quick = child;
        }
        if (!quick)
            QSKIP("no Qt Quick form factory is installed");

        constexpr int quickWidgetReady = 1; // QQuickWidget::Ready
        QCOMPARE(quick->property("status").toInt(), quickWidgetReady);

        // A name the form gets wrong is not a load error, so the evidence is
        // the diagnostic.
        QStringList unresolved;
        for (const QString &text : std::as_const(messages)) {
            if (text.contains("Unable to assign"))
                unresolved << text;
        }
        QVERIFY2(unresolved.isEmpty(), qPrintable(unresolved.join("; ")));
    }

    void testFormattingThePreviewRunsTheLanguagesFormatter()
    {
        QmlPoolTestCodeStyleFactory factory;
        CodeStyleAspect aspect(factory.globalCodeStyle(), QML_POOL_TEST_LANGUAGE_ID);

        auto preview = qobject_cast<CodeStylePreviewAspect *>(aspectNamed(&aspect, "Preview"));
        auto format = qobject_cast<ActionAspect *>(aspectNamed(&aspect, "FormatPreview"));
        auto reset = qobject_cast<ActionAspect *>(aspectNamed(&aspect, "ResetPreview"));
        QVERIFY(preview);
        QVERIFY(format);
        QVERIFY(reset);

        preview->setValue(QString("if (a) b;"));
        format->triggerAction();
        QCOMPARE(preview->value(), QString("IF (A) B;"));

        // And Reset puts the factory's text back, whatever was formatted or
        // typed over it.
        reset->triggerAction();
        QCOMPARE(preview->value(), factory.previewText());
    }

    // A language with only an indenter has nothing to run, and asks for the
    // indenting to happen again instead - which is all its formatting is.
    void testFormattingWithoutAFormatterAsksForAReindent()
    {
        QmlTestCodeStyleFactory factory;
        ICodeStylePreferences codeStyle;
        CodeStyleAspect aspect(&codeStyle, QML_TEST_LANGUAGE_ID);

        auto preview = qobject_cast<CodeStylePreviewAspect *>(aspectNamed(&aspect, "Preview"));
        auto format = qobject_cast<ActionAspect *>(aspectNamed(&aspect, "FormatPreview"));
        QVERIFY(preview);
        QVERIFY(format);

        QSignalSpy reindents(preview, &CodeStylePreviewAspect::reindentRequested);
        preview->setValue(QString("if (a) b;"));
        format->triggerAction();

        QCOMPARE(reindents.count(), 1);
        QCOMPARE(preview->value(), QString("if (a) b;"));
    }

    // A language that stayed on widgets gets none of it: there is no form to
    // put a selector in.
    void testAWidgetPageHasNoSelectorAspects()
    {
        TestCodeStyleFactory factory;
        ICodeStylePreferences codeStyle;
        CodeStyleAspect aspect(&codeStyle, TEST_LANGUAGE_ID);

        QVERIFY(!aspectNamed(&aspect, "Style"));
        QVERIFY(!aspectNamed(&aspect, "Preview"));
    }

    // Verifies the factory builds the pool + global and registers them.
    void testFactorySetupCodeStyles()
    {
        ModelTestCodeStyleFactory factory;

        ICodeStylePreferences *global = factory.globalCodeStyle();
        QVERIFY(global);
        QVERIFY(factory.codeStylePool());

        ICodeStylePreferences *builtin = factory.codeStylePool()->codeStyle("builtin");
        QVERIFY(builtin);

        // The global delegates to the built-in and resolves its tab settings.
        QCOMPARE(global->currentDelegate(), builtin);
        QCOMPARE(global->currentTabSettings().m_tabSize, 3);

        // It is registered for the language.
        QCOMPARE(codeStyleForLanguage(MODEL_TEST_LANGUAGE_ID), global);
    }
};

QObject *createCodeStyleAspectTest()
{
    return new CodeStyleAspectTest;
}

} // namespace TextEditor::Internal

#include "codestyleaspect_test.moc"

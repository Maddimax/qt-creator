// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/aspects.h>
#include <utils/infolabel.h>
#include <utils/qtcsettings_p.h>

#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QTest>

using namespace Utils;

class tst_Aspects : public QObject
{
    Q_OBJECT

private slots:
    void valuePropertyIsVolatile();
    void valuePropertyEmitsOnce();
    void valuePropertyIgnoresWrongType();
    void metadataProperties();
    void presentationReportsTheControl();
    void presentationFollowsDisplayStyle();
    void presentationCarriesBounds();
    void presentationCarriesLabelGeometry();
    void presentationCarriesNumericDisplay();
    void presentationCarriesChoiceMetadata();
    void presentationCarriesListEditingFlags();
    void presentationCarriesPlaceholderText();
    void presentationCarriesLabelState();
    void presentationCarriesColorAndResetState();
    void presentationCarriesFontFilters();
    void everyBuiltInAspectHasAControl();
    void aGuiWriteIsUndoable();
    void filePathGuiWriteIsVolatileAndUndoable();
    void integerGuiWriteIsVolatileAndUndoable();
    void integerScaleFactorKeepsStoredUnits();
    void doubleGuiWriteIsVolatileAndUndoable();
    void multiSelectionGuiWriteIsVolatileAndUndoable();
    void aContainerOnlyFreesWhatItWasToldToOwn();
    void aNestedContainerKeepsItsAspectsSettingsKeys();
    void registeringAnAspectDoesNotResetItsEnabledState();
};

// Constructing an aspect with a container registers it there but does not hand
// the container the job of freeing it, and does not make it a QObject child
// either. Every `new SomeAspect(container)` in a container that is not a
// singleton is a leak unless it says so. Written down because reading
// registerAspect() is not enough to be sure.
void tst_Aspects::aContainerOnlyFreesWhatItWasToldToOwn()
{
    QPointer<BoolAspect> registered;
    QPointer<BoolAspect> owned;
    {
        AspectContainer container;
        registered = new BoolAspect(&container);
        owned = new BoolAspect;
        container.registerAspect(owned, /*takeOwnership=*/true);
        QCOMPARE(container.aspects().size(), 2);
        // Neither is a QObject child of the container.
        QCOMPARE(registered->parent(), nullptr);
        QCOMPARE(owned->parent(), nullptr);
    }
    QVERIFY2(registered, "a container freed an aspect it was not given");
    QVERIFY2(!owned, "a container did not free an aspect it was given");
    delete registered;
}

// Writing the "value" property must not commit, so that a settings page can
// still cancel.
void tst_Aspects::valuePropertyIsVolatile()
{
    IntegerAspect aspect;
    aspect.setAutoApply(false);
    aspect.setValue(1);

    QVERIFY(aspect.setProperty("value", 42));
    QCOMPARE(aspect.volatileValue(), 42);
    QCOMPARE(aspect.value(), 1);
    QVERIFY(aspect.isDirty());
    QCOMPARE(aspect.property("value").toLongLong(), 42);

    aspect.cancel();
    QCOMPARE(aspect.volatileValue(), 1);
    QVERIFY(!aspect.isDirty());

    QVERIFY(aspect.setProperty("value", 7));
    aspect.apply();
    QCOMPARE(aspect.value(), 7);
    QVERIFY(!aspect.isDirty());
}

void tst_Aspects::valuePropertyEmitsOnce()
{
    StringAspect aspect;
    aspect.setAutoApply(false);
    aspect.setValue("before");

    QSignalSpy spy(&aspect, &BaseAspect::volatileValueChanged);
    QVERIFY(aspect.setProperty("value", "after"));
    QCOMPARE(spy.count(), 1);

    // Writing the same value again must not signal.
    QVERIFY(aspect.setProperty("value", "after"));
    QCOMPARE(spy.count(), 1);
}

void tst_Aspects::valuePropertyIgnoresWrongType()
{
    IntegerAspect aspect;
    aspect.setAutoApply(false);
    aspect.setValue(5);

    // A value that cannot become a qint64 must leave the aspect alone.
    aspect.setProperty("value", QVariant::fromValue(QStringList{"a", "b"}));
    QCOMPARE(aspect.volatileValue(), 5);
}

void tst_Aspects::metadataProperties()
{
    BoolAspect aspect;
    aspect.setLabelText("Label");
    aspect.setToolTip("Tip");

    QCOMPARE(aspect.property("labelText").toString(), QString("Label"));
    QCOMPARE(aspect.property("toolTip").toString(), QString("Tip"));
    QCOMPARE(aspect.property("enabled").toBool(), true);
    QCOMPARE(aspect.property("visible").toBool(), true);
    QCOMPARE(aspect.property("readOnly").toBool(), false);

    QSignalSpy enabledSpy(&aspect, &BaseAspect::enabledChanged);
    QVERIFY(aspect.setProperty("enabled", false));
    QCOMPARE(aspect.property("enabled").toBool(), false);
    QCOMPARE(enabledSpy.count(), 1);
}

void tst_Aspects::presentationReportsTheControl()
{
    BoolAspect boolAspect;
    boolAspect.setLabelText("Label");
    boolAspect.setToolTip("Tip");
    const AspectPresentation p = boolAspect.presentation();
    QCOMPARE(p.control, AspectControls::CheckBox);
    QCOMPARE(p.labelText, QString("Label"));
    QCOMPARE(p.toolTip, QString("Tip"));
    QVERIFY(p.visible);
    QVERIFY(p.enabled);
    QVERIFY(!p.readOnly);

    SelectionAspect selection;
    selection.addOption("One");
    selection.addOption("Two");
    selection.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    QCOMPARE(selection.presentation().control, AspectControls::ComboBox);
    const QList<AspectPresentation::Choice> choices = selection.presentation().choices;
    QCOMPARE(choices.size(), 2);
    QCOMPARE(choices.at(0).display, QString("One"));
    QCOMPARE(choices.at(1).display, QString("Two"));

    AspectContainer container;
    QCOMPARE(container.presentation().control, AspectControls::Container);
}

// The control has to follow the display style, which is the part a renderer
// cannot work out from the aspect's type.
void tst_Aspects::presentationFollowsDisplayStyle()
{
    BoolAspect boolAspect;
    QCOMPARE(boolAspect.presentation().control, AspectControls::CheckBox);
    boolAspect.setDisplayStyle(BoolAspect::DisplayStyle::RadionButton);
    QCOMPARE(boolAspect.presentation().control, AspectControls::RadioButton);

    StringAspect stringAspect;
    QCOMPARE(stringAspect.presentation().control, AspectControls::Label);
    stringAspect.setDisplayStyle(StringAspect::LineEditDisplay);
    QCOMPARE(stringAspect.presentation().control, AspectControls::LineEdit);
    stringAspect.setDisplayStyle(StringAspect::PasswordLineEditDisplay);
    QCOMPARE(stringAspect.presentation().control, AspectControls::PasswordLineEdit);
    stringAspect.setDisplayStyle(StringAspect::TextEditDisplay);
    QCOMPARE(stringAspect.presentation().control, AspectControls::TextEdit);

    SelectionAspect selection;
    selection.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
    QCOMPARE(selection.presentation().control, AspectControls::RadioButtonGroup);

    StringListAspect list;
    QCOMPARE(list.presentation().control, AspectControls::StringList);
    list.setDisplayStyle(StringListAspect::DisplayStyle::CommaSeparatedLineEdit);
    QCOMPARE(list.presentation().control, AspectControls::CommaSeparatedLineEdit);
}

// An unset bound must stay unset, so that a renderer can tell "no minimum"
// from "minimum happens to be zero".
void tst_Aspects::presentationCarriesBounds()
{
    IntegerAspect integer;
    QVERIFY(!integer.presentation().minimum.isValid());
    QVERIFY(!integer.presentation().maximum.isValid());
    integer.setRange(3, 9);
    integer.setSingleStep(2);
    QCOMPARE(integer.presentation().minimum.toLongLong(), 3);
    QCOMPARE(integer.presentation().maximum.toLongLong(), 9);
    QCOMPARE(integer.presentation().singleStep.toLongLong(), 2);

    DoubleAspect real;
    QVERIFY(!real.presentation().minimum.isValid());
    real.setRange(0.5, 1.5);
    QCOMPARE(real.presentation().control, AspectControls::DoubleSpinBox);
    QCOMPARE(real.presentation().minimum.toDouble(), 0.5);
    QCOMPARE(real.presentation().maximum.toDouble(), 1.5);
}

// The label decision lives in the aspect: placement and span must reach a
// renderer without a widget being built. (The pixmap is carried too, but a
// non-null QPixmap needs a QGuiApplication, which this test does not have.)
void tst_Aspects::presentationCarriesLabelGeometry()
{
    IntegerAspect integer;
    QCOMPARE(integer.presentation().labelPlacement, AspectControls::LabelPlacement::InExtraLabel);
    QCOMPARE(integer.presentation().spanX, 2);
    QCOMPARE(integer.presentation().spanY, 1);
    integer.setSpan(3, 2);
    QCOMPARE(integer.presentation().spanX, 3);
    QCOMPARE(integer.presentation().spanY, 2);
    QVERIFY(integer.presentation().labelPixmap.isNull());

    BoolAspect boolAspect;
    QCOMPARE(boolAspect.presentation().labelPlacement, AspectControls::LabelPlacement::AtControl);
    boolAspect.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    QCOMPARE(boolAspect.presentation().labelPlacement, AspectControls::LabelPlacement::Compact);
    boolAspect.setLabel("With tip", BoolAspect::LabelPlacement::ShowTip);
    QCOMPARE(boolAspect.presentation().labelPlacement, AspectControls::LabelPlacement::ShowTip);
    boolAspect.setLabelPlacement(BoolAspect::LabelPlacement::InExtraLabel);
    QCOMPARE(boolAspect.presentation().labelPlacement,
             AspectControls::LabelPlacement::InExtraLabel);
}

// The spin box decorations are display-only state: value() never contains
// them, so a renderer can only get them from the descriptor.
void tst_Aspects::presentationCarriesNumericDisplay()
{
    IntegerAspect integer;
    QCOMPARE(integer.presentation().displayIntegerBase, 10);
    QCOMPARE(integer.presentation().displayScaleFactor, 1);
    integer.setPrefix("0x");
    integer.setSuffix(" MB");
    integer.setSpecialValueText("<unset>");
    integer.setDisplayIntegerBase(16);
    integer.setDisplayScaleFactor(1024);
    const AspectPresentation p = integer.presentation();
    QCOMPARE(p.prefix, QString("0x"));
    QCOMPARE(p.suffix, QString(" MB"));
    QCOMPARE(p.specialValueText, QString("<unset>"));
    QCOMPARE(p.displayIntegerBase, 16);
    QCOMPARE(p.displayScaleFactor, 1024);

    DoubleAspect real;
    real.setPrefix("~");
    real.setSuffix(" s");
    real.setSpecialValueText("default");
    const AspectPresentation q = real.presentation();
    QCOMPARE(q.prefix, QString("~"));
    QCOMPARE(q.suffix, QString(" s"));
    QCOMPARE(q.specialValueText, QString("default"));
}

// addOption()'s tool tip, enabled flag and item data must survive into the
// descriptor: a combo delegate writes back the id, not the display text.
void tst_Aspects::presentationCarriesChoiceMetadata()
{
    SelectionAspect selection;
    selection.addOption("One", "first tip");
    SelectionAspect::Option second("Two", "second tip", 42);
    second.enabled = false;
    selection.addOption(second);

    const QList<AspectPresentation::Choice> choices = selection.presentation().choices;
    QCOMPARE(choices.size(), 2);
    QCOMPARE(choices.at(0).display, QString("One"));
    QCOMPARE(choices.at(0).toolTip, QString("first tip"));
    QVERIFY(choices.at(0).enabled);
    QCOMPARE(choices.at(1).display, QString("Two"));
    QCOMPARE(choices.at(1).toolTip, QString("second tip"));
    QVERIFY(!choices.at(1).enabled);
    QCOMPARE(choices.at(1).id.toInt(), 42);

    // A multi selection writes back the display strings, so they are the ids.
    MultiSelectionAspect multi;
    multi.setAllValues({"a", "b"});
    const QList<AspectPresentation::Choice> all = multi.presentation().choices;
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.at(0).display, QString("a"));
    QCOMPARE(all.at(0).id.toString(), QString("a"));
    QVERIFY(all.at(1).enabled);
    QCOMPARE(all.at(1).id.toString(), QString("b"));
}

void tst_Aspects::presentationCarriesListEditingFlags()
{
    StringListAspect list;
    QVERIFY(list.presentation().allowAdding);
    QVERIFY(list.presentation().allowRemoving);
    QVERIFY(list.presentation().allowEditing);
    list.setUiAllowAdding(false);
    list.setUiAllowRemoving(false);
    list.setUiAllowEditing(false);
    const AspectPresentation p = list.presentation();
    QVERIFY(!p.allowAdding);
    QVERIFY(!p.allowRemoving);
    QVERIFY(!p.allowEditing);
}

void tst_Aspects::presentationCarriesPlaceholderText()
{
    FilePathListAspect paths;
    QVERIFY(paths.presentation().placeholderText.isEmpty());
    paths.setPlaceHolderText("One path per line");
    QCOMPARE(paths.placeHolderText(), QString("One path per line"));
    QCOMPARE(paths.presentation().placeholderText, QString("One path per line"));
}

void tst_Aspects::presentationCarriesLabelState()
{
    TextDisplay display(nullptr, "message");
    QCOMPARE(display.presentation().infoType, AspectControls::InfoType::None);
    QVERIFY(display.presentation().wordWrap);
    display.setIconType(InfoType::Warning);
    display.setWordWrap(false);
    const AspectPresentation p = display.presentation();
    QCOMPARE(p.infoType, AspectControls::InfoType::Warning);
    QVERIFY(!p.wordWrap);
}

void tst_Aspects::presentationCarriesColorAndResetState()
{
    ColorAspect color;
    QVERIFY(color.presentation().alphaAllowed);
    QVERIFY(color.presentation().withResetButton);
    color.setAlphaAllowed(false);
    color.setWithResetButton(false);
    QVERIFY(!color.presentation().alphaAllowed);
    QVERIFY(!color.presentation().withResetButton);

    StringAspect string;
    string.setDisplayStyle(StringAspect::LineEditDisplay);
    QVERIFY(!string.presentation().withResetButton);
    string.setUseResetButton();
    QVERIFY(string.presentation().withResetButton);
}

void tst_Aspects::presentationCarriesFontFilters()
{
    FontFamilyAspect font;
    QCOMPARE(font.presentation().fontFilters,
             AspectControls::FontFilters(AspectControls::AllFonts));
    font.setFontFilters(FontFamilyAspect::MonospacedFonts);
    QCOMPARE(font.presentation().fontFilters,
             AspectControls::FontFilters(AspectControls::MonospacedFonts));
}

// A built-in aspect reporting Custom means presentation() was not overridden
// and every renderer will fall back to a placeholder for it.
void tst_Aspects::everyBuiltInAspectHasAControl()
{
    BoolAspect boolAspect;
    ColorAspect color;
    DoubleAspect real;
    FilePathAspect filePath;
    FilePathListAspect filePathList;
    FontFamilyAspect fontFamily;
    IntegerAspect integer;
    IntegersAspect integers;
    MultiSelectionAspect multiSelection;
    SelectionAspect selection;
    StringAspect string;
    StringListAspect stringList;
    StringSelectionAspect stringSelection;
    TextDisplay textDisplay;

    const QList<QPair<QString, BaseAspect *>> aspects = {
        {"BoolAspect", &boolAspect},
        {"ColorAspect", &color},
        {"DoubleAspect", &real},
        {"FilePathAspect", &filePath},
        {"FilePathListAspect", &filePathList},
        {"FontFamilyAspect", &fontFamily},
        {"IntegerAspect", &integer},
        {"IntegersAspect", &integers},
        {"MultiSelectionAspect", &multiSelection},
        {"SelectionAspect", &selection},
        {"StringAspect", &string},
        {"StringListAspect", &stringList},
        {"StringSelectionAspect", &stringSelection},
        {"TextDisplay", &textDisplay},
    };

    for (const auto &[name, aspect] : aspects) {
        const AspectControls::Control control = aspect->presentation().control;
        QVERIFY2(control != AspectControls::Custom, qPrintable(name));
    }
}

// A user editing a control has to be undoable. The widget delegates push an
// undo command from their signal handlers; anything driving the aspect through
// its properties, as a QML delegate does, must get the same.
void tst_Aspects::aGuiWriteIsUndoable()
{
    QUndoStack stack;
    BoolAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setValue(false);

    QCOMPARE(stack.count(), 0);
    QVERIFY(aspect.setProperty("value", true));
    QCOMPARE(aspect.volatileValue(), true);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), false);
}

void tst_Aspects::filePathGuiWriteIsVolatileAndUndoable()
{
    QUndoStack stack;
    FilePathAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setValue(QString("/old"));

    QCOMPARE(stack.count(), 0);
    QVERIFY(aspect.setProperty("value", QString("/new")));
    QCOMPARE(aspect.volatileValue(), QString("/new"));
    QCOMPARE(aspect.value(), QString("/old"));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), QString("/old"));

    stack.redo();
    aspect.apply();
    QCOMPARE(aspect.value(), QString("/new"));
}

void tst_Aspects::integerGuiWriteIsVolatileAndUndoable()
{
    QUndoStack stack;
    IntegerAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setValue(1);

    QCOMPARE(stack.count(), 0);
    QVERIFY(aspect.setProperty("value", 42));
    QCOMPARE(aspect.volatileValue(), 42);
    QCOMPARE(aspect.value(), 1);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), 1);

    stack.redo();
    aspect.apply();
    QCOMPARE(aspect.value(), 42);
}

// The GUI-side state holds the stored value: a display scale factor must not
// leak into value() or volatileValue(); it only rescales what a spin box shows.
void tst_Aspects::integerScaleFactorKeepsStoredUnits()
{
    QUndoStack stack;
    IntegerAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setDisplayScaleFactor(1024);
    aspect.setValue(2048);

    QCOMPARE(aspect.volatileValue(), 2048);
    QCOMPARE(aspect.property("value").toLongLong(), 2048);

    QVERIFY(aspect.setProperty("value", 4096));
    QCOMPARE(aspect.volatileValue(), 4096);
    QCOMPARE(aspect.value(), 2048);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), 2048);

    stack.redo();
    aspect.apply();
    QCOMPARE(aspect.value(), 4096);
}

void tst_Aspects::doubleGuiWriteIsVolatileAndUndoable()
{
    QUndoStack stack;
    DoubleAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setValue(1.5);

    QCOMPARE(stack.count(), 0);
    QVERIFY(aspect.setProperty("value", 2.5));
    QCOMPARE(aspect.volatileValue(), 2.5);
    QCOMPARE(aspect.value(), 1.5);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), 1.5);

    stack.redo();
    aspect.apply();
    QCOMPARE(aspect.value(), 2.5);
}

void tst_Aspects::multiSelectionGuiWriteIsVolatileAndUndoable()
{
    QUndoStack stack;
    MultiSelectionAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setAllValues({"a", "b", "c"});
    aspect.setValue({"a"});

    QCOMPARE(stack.count(), 0);
    QVERIFY(aspect.setProperty("value", QStringList({"a", "c"})));
    QCOMPARE(aspect.volatileValue(), QStringList({"a", "c"}));
    QCOMPARE(aspect.value(), QStringList({"a"}));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), QStringList({"a"}));

    stack.redo();
    aspect.apply();
    QCOMPARE(aspect.value(), QStringList({"a", "c"}));
}


void tst_Aspects::aNestedContainerKeepsItsAspectsSettingsKeys()
{
    // A container nested only so that a Qt Quick form can address its aspects
    // by name must not move their settings. The group belongs to the outer
    // container, and a nested one that added its own would orphan every
    // setting a user already has.
    AspectContainer outer;
    outer.setSettingsGroup("Outer");
    AspectContainer nested(&outer);

    BoolAspect direct(&outer);
    direct.setSettingsKey("Direct");
    BoolAspect inNested(&nested);
    inNested.setSettingsKey("Nested");

    direct.setValue(true);
    inNested.setValue(true);
    outer.writeSettings();
    userSettings().sync();

    const QStringList keys = userSettings().allKeys();
    QVERIFY2(keys.contains("Outer/Direct"), qPrintable(keys.join(", ")));
    QVERIFY2(keys.contains("Outer/Nested"),
             qPrintable("a nested container moved its aspects' settings: " + keys.join(", ")));

    // And what was written is what comes back.
    direct.setValue(false);
    inNested.setValue(false);
    outer.readSettings();
    QCOMPARE(direct.value(), true);
    QCOMPARE(inNested.value(), true);
}

// A container's setEnabled() pushes its value onto every child, so registering
// a child must not re-assert "enabled" as a matter of course: a container that
// disables part of itself before being registered has to keep that. Found on
// the C++ Code Style page, where the category groups are disabled for a
// read-only style and were editable anyway.
void tst_Aspects::registeringAnAspectDoesNotResetItsEnabledState()
{
    AspectContainer page;

    AspectContainer settings;
    BoolAspect locked(&settings);
    locked.setEnabled(false);
    QVERIFY(!locked.isEnabled());

    page.registerAspect(&settings);
    QVERIFY2(!locked.isEnabled(), "registering re-enabled an aspect that was disabled");
    QVERIFY(settings.isEnabled());

    // And the other way round: registering into a disabled container disables
    // what goes into it, which is what makes a disabled group disable its rows.
    AspectContainer disabledPage;
    disabledPage.setEnabled(false);

    AspectContainer more;
    BoolAspect row(&more);
    QVERIFY(row.isEnabled());

    disabledPage.registerAspect(&more);
    QVERIFY2(!more.isEnabled(), "a disabled container accepted an enabled child");
    QVERIFY2(!row.isEnabled(), "the child's own rows stayed enabled");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Aspects that read and write settings need somewhere to do it, and only
    // main() is allowed to install it.
    QTemporaryDir settingsDir;
    Internal::SettingsSetup::setupSettings(
        new QtcSettings(settingsDir.filePath("user.ini"), QSettings::IniFormat),
        new QtcSettings);

    tst_Aspects tc;
    QTEST_SET_MAIN_SOURCE_PATH
    const int result = QTest::qExec(&tc, argc, argv);
    Internal::SettingsSetup::destroySettings();
    return result;
}

#include "tst_aspects.moc"

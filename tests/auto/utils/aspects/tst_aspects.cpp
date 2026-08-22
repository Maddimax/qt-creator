// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/aspects.h>

#include <QSignalSpy>
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
    void everyBuiltInAspectHasAControl();
    void aGuiWriteIsUndoable();
};

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
    QCOMPARE(selection.presentation().choices, QStringList({"One", "Two"}));

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

QTEST_GUILESS_MAIN(tst_Aspects)

#include "tst_aspects.moc"

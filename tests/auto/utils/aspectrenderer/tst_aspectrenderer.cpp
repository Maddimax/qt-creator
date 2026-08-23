// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/aspects.h>
#include <utils/aspectwidgetrenderer.h>
#include <utils/elidinglabel.h>
#include <utils/fancylineedit.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/passworddialog.h>
#include <utils/pathchooser.h>
#include <utils/pathlisteditor.h>
#include <utils/qtcolorbutton.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItem>
#include <QTest>
#include <QTextEdit>
#include <QTreeWidget>
#include <QUndoStack>

#include <memory>

using namespace Utils;

// Every test runs twice: once through the installed widget renderer and once
// through the aspects' inline widget construction, asserting that both paths
// build the same control wired the same way.
// Only the aspects that still own an inline addToLayoutImpl body have a
// fallback to compare against. The rest delegate unconditionally, so rendering
// them with no renderer installed produces nothing - which their QTC_CHECK
// says out loud.
static void addRendererRows()
{
    QTest::addColumn<bool>("withRenderer");
    QTest::newRow("renderer") << true;
}

// StringAspect and FilePathAspect keep their bodies, because the checkable
// composites interleave the checker with the control's construction.
static void addRendererAndFallbackRows()
{
    QTest::addColumn<bool>("withRenderer");
    QTest::newRow("renderer") << true;
    QTest::newRow("fallback") << false;
}

static void setRendererInstalled(bool installed)
{
    Utils::setAspectRenderer(installed ? AspectRenderer(&Internal::renderAspectToLayout)
                                       : AspectRenderer());
}

static std::unique_ptr<QWidget> render(BaseAspect &aspect)
{
    return std::unique_ptr<QWidget>(Layouting::Column { aspect }.emerge());
}

class tst_AspectRenderer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void checkBox_data() { addRendererRows(); }
    void checkBox();
    void radioButton_data() { addRendererRows(); }
    void radioButton();
    void comboBox_data() { addRendererRows(); }
    void comboBox();
    void radioButtonGroup_data() { addRendererRows(); }
    void radioButtonGroup();
    void spinBox_data() { addRendererRows(); }
    void spinBox();
    void spinBoxScaled_data() { addRendererRows(); }
    void spinBoxScaled();
    void doubleSpinBox_data() { addRendererRows(); }
    void doubleSpinBox();
    void fontFamilyPicker_data() { addRendererRows(); }
    void fontFamilyPicker();
    void multiSelection_data() { addRendererRows(); }
    void multiSelection();
    void commaSeparatedLineEdit_data() { addRendererRows(); }
    void commaSeparatedLineEdit();
    void filePathList_data() { addRendererRows(); }
    void filePathList();
    void integerList_data() { addRendererRows(); }
    void integerList();
    void colorPicker_data() { addRendererRows(); }
    void colorPicker();
    void textDisplay_data() { addRendererRows(); }
    void textDisplay();
    void stringLabel_data() { addRendererAndFallbackRows(); }
    void stringLabel();
    void stringLineEdit_data() { addRendererAndFallbackRows(); }
    void stringLineEdit();
    void stringTextEdit_data() { addRendererAndFallbackRows(); }
    void stringTextEdit();
    void stringPasswordLineEdit_data() { addRendererAndFallbackRows(); }
    void stringPasswordLineEdit();
    void pathChooser_data() { addRendererAndFallbackRows(); }
    void pathChooser();
    void stringListTree_data() { addRendererRows(); }
    void stringListTree();
    void stringSelection_data() { addRendererRows(); }
    void stringSelection();
    void fontPicker_data() { addRendererRows(); }
    void fontPicker();
};

void tst_AspectRenderer::initTestCase()
{
    // The official entry point; individual tests switch the renderer through
    // setAspectRenderer() to also cover the fallback.
    Utils::installAspectWidgetRenderer();
}

void tst_AspectRenderer::checkBox()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    BoolAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setLabelText("Flag");

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto checkBox = widget->findChild<QCheckBox *>();
    QVERIFY(checkBox);
    QCOMPARE(checkBox->text(), QString("Flag")); // default placement: AtCheckBox
    QVERIFY(!checkBox->isChecked());

    checkBox->click();
    QCOMPARE(aspect.volatileValue(), true);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), false);
    QVERIFY(!checkBox->isChecked());

    aspect.setVolatileValue(true);
    QVERIFY(checkBox->isChecked());
}

void tst_AspectRenderer::radioButton()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    BoolAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setDisplayStyle(BoolAspect::DisplayStyle::RadionButton);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto radioButton = widget->findChild<QRadioButton *>();
    QVERIFY(radioButton);
    QVERIFY(!radioButton->isChecked());

    radioButton->click();
    QCOMPARE(aspect.volatileValue(), true);
    QCOMPARE(stack.count(), 1);

    aspect.setVolatileValue(false);
    QVERIFY(!radioButton->isChecked());
}

void tst_AspectRenderer::comboBox()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    SelectionAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    aspect.setLabelText("Choice");
    aspect.addOption("One");
    aspect.addOption("Two");

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto comboBox = widget->findChild<QComboBox *>();
    QVERIFY(comboBox);
    QCOMPARE(comboBox->count(), 2);
    QCOMPARE(comboBox->currentIndex(), 0);
    auto label = widget->findChild<QLabel *>();
    QVERIFY(label);
    QCOMPARE(label->text(), QString("Choice"));

    comboBox->setCurrentIndex(1);
    QCOMPARE(aspect.volatileValue(), 1);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), 0);
    QCOMPARE(comboBox->currentIndex(), 0);

    aspect.setVolatileValue(1);
    QCOMPARE(comboBox->currentIndex(), 1);
}

void tst_AspectRenderer::radioButtonGroup()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    SelectionAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.addOption("One");
    aspect.addOption("Two");

    const std::unique_ptr<QWidget> widget = render(aspect);
    const QList<QRadioButton *> buttons = widget->findChildren<QRadioButton *>();
    QCOMPARE(buttons.size(), 2);
    QVERIFY(buttons.at(0)->isChecked());

    buttons.at(1)->click();
    QCOMPARE(aspect.volatileValue(), 1);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), 0);
    QVERIFY(buttons.at(0)->isChecked());

    aspect.setVolatileValue(1);
    QVERIFY(buttons.at(1)->isChecked());
}

void tst_AspectRenderer::spinBox()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    IntegerAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setLabelText("Number");
    aspect.setRange(0, 100);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto spinBox = widget->findChild<QSpinBox *>();
    QVERIFY(spinBox);
    QCOMPARE(spinBox->minimum(), 0);
    QCOMPARE(spinBox->maximum(), 100);
    auto label = widget->findChild<QLabel *>();
    QVERIFY(label);
    QCOMPARE(label->text(), QString("Number"));

    spinBox->setValue(42);
    QCOMPARE(aspect.volatileValue(), 42);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), 0);
    QCOMPARE(spinBox->value(), 0);

    aspect.setVolatileValue(7);
    QCOMPARE(spinBox->value(), 7);
}

void tst_AspectRenderer::spinBoxScaled()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    IntegerAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setRange(0, 10240);
    aspect.setDisplayScaleFactor(1024);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto spinBox = widget->findChild<QSpinBox *>();
    QVERIFY(spinBox);
    QCOMPARE(spinBox->maximum(), 10);

    spinBox->setValue(2);
    QCOMPARE(aspect.volatileValue(), 2048); // The aspect stores unscaled units.
    QCOMPARE(stack.count(), 1);

    aspect.setVolatileValue(4096);
    QCOMPARE(spinBox->value(), 4);
}

void tst_AspectRenderer::doubleSpinBox()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    DoubleAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setRange(0.0, 10.0);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto spinBox = widget->findChild<QDoubleSpinBox *>();
    QVERIFY(spinBox);
    QCOMPARE(spinBox->maximum(), 10.0);

    spinBox->setValue(2.5);
    QCOMPARE(aspect.volatileValue(), 2.5);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), 0.0);
    QCOMPARE(spinBox->value(), 0.0);

    aspect.setVolatileValue(1.5);
    QCOMPARE(spinBox->value(), 1.5);
}

void tst_AspectRenderer::fontFamilyPicker()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    FontFamilyAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto comboBox = widget->findChild<QFontComboBox *>();
    QVERIFY(comboBox);

    QString target;
    for (int i = 0; i < comboBox->count(); ++i) {
        if (comboBox->itemText(i) != comboBox->currentText()) {
            target = comboBox->itemText(i);
            break;
        }
    }
    if (target.isEmpty())
        QSKIP("Not enough font families available");

    comboBox->setCurrentText(target);
    QCOMPARE(aspect.volatileValue(), target);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QVERIFY(aspect.volatileValue().isEmpty());

    // The font family picker has no aspect-to-widget sync; the combo box must
    // keep its selection when the volatile value changes from outside.
    aspect.setVolatileValue(comboBox->itemText(0));
    QCOMPARE(comboBox->currentText(), target);
}

void tst_AspectRenderer::multiSelection()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    MultiSelectionAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setAllValues({"a", "b", "c"});

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto listWidget = widget->findChild<QListWidget *>();
    QVERIFY(listWidget);
    QCOMPARE(listWidget->count(), 3);
    QCOMPARE(listWidget->item(0)->checkState(), Qt::Unchecked);

    listWidget->item(0)->setCheckState(Qt::Checked);
    QCOMPARE(aspect.volatileValue(), QStringList("a"));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QVERIFY(aspect.volatileValue().isEmpty());
    QCOMPARE(listWidget->item(0)->checkState(), Qt::Unchecked);

    aspect.setVolatileValue(QStringList("b"));
    QCOMPARE(listWidget->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(listWidget->item(1)->checkState(), Qt::Checked);
}

void tst_AspectRenderer::commaSeparatedLineEdit()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    StringListAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setDisplayStyle(StringListAspect::DisplayStyle::CommaSeparatedLineEdit);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto lineEdit = widget->findChild<FancyLineEdit *>();
    QVERIFY(lineEdit);
    QVERIFY(lineEdit->text().isEmpty());

    lineEdit->insert("a, b"); // Emits textEdited, like typing.
    QCOMPARE(aspect.volatileValue(), (QStringList{"a", "b"}));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QVERIFY(aspect.volatileValue().isEmpty());
    QVERIFY(lineEdit->text().isEmpty());

    aspect.setVolatileValue(QStringList{"x", "y"});
    QCOMPARE(lineEdit->text(), QString("x,y"));
}

void tst_AspectRenderer::filePathList()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    FilePathListAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto editor = widget->findChild<PathListEditor *>();
    QVERIFY(editor);
    QVERIFY(editor->pathList().isEmpty());

    editor->setPathList(QStringList("/tmp/one")); // Fires changed(), like typing.
    QCOMPARE(aspect.volatileValue(), QStringList("/tmp/one"));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QVERIFY(aspect.volatileValue().isEmpty());
    QVERIFY(editor->pathList().isEmpty());

    aspect.setVolatileValue(QStringList("/tmp/two"));
    QCOMPARE(editor->pathList(), QStringList("/tmp/two"));
}

void tst_AspectRenderer::integerList()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    IntegersAspect aspect;
    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    QVERIFY(widget->findChildren<QWidget *>().isEmpty()); // Renders nothing.
}

void tst_AspectRenderer::colorPicker()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    ColorAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setDefaultValue(QColor(Qt::black));

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto button = widget->findChild<QtColorButton *>();
    QVERIFY(button);
    QCOMPARE(button->color(), QColor(Qt::black));
    QCOMPARE(button->minimumSize(), QSize(64, 0)); // ColorAspect's default.
    auto resetButton = widget->findChild<QPushButton *>();
    QVERIFY(resetButton); // withResetButton is on by default.

    // QtColorButton::setColor() does not emit colorChanged(); emit it the way
    // the button's color dialog does.
    emit button->colorChanged(QColor(Qt::red));
    QCOMPARE(aspect.volatileValue(), QColor(Qt::red));
    QCOMPARE(stack.count(), 0); // ColorAspect records no undo, on either path.

    aspect.setVolatileValue(QColor(Qt::green));
    QCOMPARE(button->color(), QColor(Qt::green));

    resetButton->click();
    QCOMPARE(aspect.volatileValue(), QColor(Qt::black));
}

void tst_AspectRenderer::textDisplay()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    TextDisplay aspect(nullptr, "hello");
    const std::unique_ptr<QWidget> widget = render(aspect);
    // InfoLabel has no Q_OBJECT macro, so findChild cannot key on it.
    auto label = dynamic_cast<InfoLabel *>(widget->findChild<ElidingLabel *>());
    QVERIFY(label);
    QCOMPARE(label->text(), QString("hello"));
    QVERIFY(label->wordWrap()); // TextDisplay defaults to word wrap.

    aspect.setText("world");
    QCOMPARE(label->text(), QString("world"));

    // The live-forwarding setters must reach the built label.
    aspect.setWordWrap(false);
    QVERIFY(!label->wordWrap());
    aspect.setIconType(InfoLabelType::Error);
    QCOMPARE(label->type(), InfoLabelType::Error);
}

void tst_AspectRenderer::stringLabel()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    StringAspect aspect; // Default display style: LabelDisplay.
    aspect.setAutoApply(false);
    aspect.setValue("abc");

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto label = widget->findChild<ElidingLabel *>();
    QVERIFY(label);
    QCOMPARE(label->text(), QString("abc"));

    aspect.setVolatileValue("xyz");
    QCOMPARE(label->text(), QString("xyz"));
}

void tst_AspectRenderer::stringLineEdit()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    StringAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setDisplayStyle(StringAspect::LineEditDisplay);
    aspect.setLabelText("Name");

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto lineEdit = widget->findChild<FancyLineEdit *>();
    QVERIFY(lineEdit);
    QVERIFY(lineEdit->text().isEmpty());
    auto label = widget->findChild<QLabel *>();
    QVERIFY(label);
    QCOMPARE(label->text(), QString("Name"));

    lineEdit->insert("abc");
    QCOMPARE(aspect.volatileValue(), QString("abc"));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QVERIFY(aspect.volatileValue().isEmpty());
    QVERIFY(lineEdit->text().isEmpty());

    aspect.setVolatileValue("zz");
    QCOMPARE(lineEdit->text(), QString("zz"));
}

void tst_AspectRenderer::stringTextEdit()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    StringAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setDisplayStyle(StringAspect::TextEditDisplay);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto textEdit = widget->findChild<QTextEdit *>();
    QVERIFY(textEdit);
    QVERIFY(textEdit->toPlainText().isEmpty());

    textEdit->setPlainText("abc");
    QCOMPARE(aspect.volatileValue(), QString("abc"));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QVERIFY(aspect.volatileValue().isEmpty());
    QVERIFY(textEdit->toPlainText().isEmpty());

    aspect.setVolatileValue("qq");
    QCOMPARE(textEdit->toPlainText(), QString("qq"));
}

void tst_AspectRenderer::stringPasswordLineEdit()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    StringAspect aspect;
    aspect.setAutoApply(false);
    aspect.setDisplayStyle(StringAspect::PasswordLineEditDisplay);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto lineEdit = widget->findChild<FancyLineEdit *>();
    QVERIFY(lineEdit);
    QCOMPARE(lineEdit->echoMode(), QLineEdit::PasswordEchoOnEdit);

    // ShowPasswordButton has no Q_OBJECT macro, so findChild cannot key on it.
    ShowPasswordButton *showButton = nullptr;
    const QList<QAbstractButton *> buttons = widget->findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons) {
        if (auto b = dynamic_cast<ShowPasswordButton *>(button))
            showButton = b;
    }
    QVERIFY(showButton);
    showButton->click();
    QCOMPARE(lineEdit->echoMode(), QLineEdit::Normal);
    showButton->click();
    QCOMPARE(lineEdit->echoMode(), QLineEdit::PasswordEchoOnEdit);
}

void tst_AspectRenderer::pathChooser()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    FilePathAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto chooser = widget->findChild<PathChooser *>();
    QVERIFY(chooser);
    QCOMPARE(aspect.pathChooser(), chooser); // The cache keeps serving the public API.

    chooser->lineEdit()->insert("/tmp/one");
    QCOMPARE(aspect.volatileValue(), QString("/tmp/one"));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QVERIFY(aspect.volatileValue().isEmpty());
    QVERIFY(chooser->lineEdit()->text().isEmpty());

    aspect.setVolatileValue("/tmp/two");
    QCOMPARE(chooser->lineEdit()->text(), QString("/tmp/two"));
}

void tst_AspectRenderer::stringListTree()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    StringListAspect aspect; // Default display style: ListView (the tree editor).
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setValue({"a", "b"});

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto tree = widget->findChild<QTreeWidget *>();
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->text(0), QString("a"));
    QCOMPARE(widget->findChildren<QPushButton *>().size(), 2); // Add and Remove.

    tree->topLevelItem(0)->setText(0, "z"); // Fires dataChanged, like an item edit.
    QCOMPARE(aspect.volatileValue(), (QStringList{"z", "b"}));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), (QStringList{"a", "b"}));
    QCOMPARE(tree->topLevelItem(0)->text(0), QString("a"));

    aspect.setVolatileValue(QStringList{"x"});
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->text(0), QString("x"));
}

void tst_AspectRenderer::stringSelection()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    StringSelectionAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setFillCallback([](const StringSelectionAspect::ResultCallback &cb) {
        const auto makeItem = [](const QString &display, const QString &id) {
            auto item = new QStandardItem(display);
            item->setData(id); // Qt::UserRole + 1, the write-back id.
            return item;
        };
        cb({makeItem("One", "one"), makeItem("Two", "two")});
    });
    aspect.setValue("one");

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto comboBox = widget->findChild<QComboBox *>();
    QVERIFY(comboBox);
    QCOMPARE(comboBox->count(), 2);
    QCOMPARE(comboBox->currentIndex(), 0);

    emit comboBox->activated(1); // Only user interaction emits this.
    QCOMPARE(aspect.volatileValue(), QString("two"));
    QCOMPARE(stack.count(), 1);
    QCOMPARE(comboBox->currentIndex(), 1);

    stack.undo();
    QCOMPARE(aspect.volatileValue(), QString("one"));
    QCOMPARE(comboBox->currentIndex(), 0);

    aspect.setVolatileValue("two");
    QCOMPARE(comboBox->currentIndex(), 1);
}

void tst_AspectRenderer::fontPicker()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    FontAspect aspect;
    const std::unique_ptr<QWidget> widget = render(aspect);
    auto familyBox = widget->findChild<QFontComboBox *>();
    QVERIFY(familyBox);

    QComboBox *sizeBox = nullptr;
    const QList<QComboBox *> boxes = widget->findChildren<QComboBox *>();
    for (QComboBox *box : boxes) {
        if (!qobject_cast<QFontComboBox *>(box))
            sizeBox = box;
    }
    QVERIFY(sizeBox);
    QVERIFY(sizeBox->count() > 0);

    const int target = sizeBox->currentIndex() == 0 ? sizeBox->count() - 1 : 0;
    if (target == sizeBox->currentIndex())
        QSKIP("Only one point size available");

    sizeBox->setCurrentIndex(target);
    QCOMPARE(aspect.fontPointSize.volatileValue(), qint64(sizeBox->itemData(target).toInt()));
}

QTEST_MAIN(tst_AspectRenderer)

#include "tst_aspectrenderer.moc"

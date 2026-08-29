// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/aspectlist.h>
#include <utils/aspectwidgets.h>
#include <utils/aspectwidgetrenderer.h>
#include <utils/elidinglabel.h>
#include <utils/fancylineedit.h>
#include <utils/environmentchangesaspect.h>
#include <utils/terminalcommandaspect.h>
#include <utils/groupedlistaspect.h>
#include <utils/groupedmodel.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/passworddialog.h>
#include <utils/pathchooser.h>
#include <utils/pathlisteditor.h>
#include <utils/qtcolorbutton.h>

#include <QCheckBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QComboBox>
#include <QFontComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QGridLayout>
#include <QSpinBox>
#include <QSignalSpy>
#include <QStandardItem>
#include <QTest>
#include <QTextEdit>
#include <QTreeWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QUndoStack>

#include <memory>

using namespace Utils;

namespace {

class RendererTool
{
public:
    friend bool operator==(const RendererTool &, const RendererTool &) = default;

    QString name;
    bool autoDetected = false;
};

class RendererToolsModel : public TypedGroupedModel<RendererTool>
{
public:
    RendererToolsModel()
    {
        setHeader({"Name"});
        setFilters("Auto-detected", {{"Manual", [this](int row) {
                                          return !item(row).autoDetected;
                                      }}});
        appendItem({"found", true});
        appendItem({"mine", false});
    }

private:
    QVariant variantData(int row, int column, int role) const override
    {
        if (role == Qt::DisplayRole && column == 0)
            return item(row).name;
        return {};
    }
};

} // namespace

// No aspect owns an inline addToLayoutImpl body any more: they all delegate
// unconditionally, so rendering with no renderer installed produces nothing -
// which their QTC_CHECK says out loud. The column is kept so that the tests
// name the path they exercise.
static void addRendererRows()
{
    QTest::addColumn<bool>("withRenderer");
    QTest::newRow("renderer") << true;
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

    void spanReachesAcrossColumnsAndIsQuietWhereThereAreNone();
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
    void groupedList_data() { addRendererRows(); }
    void groupedList();
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
    void stringLabel_data() { addRendererRows(); }
    void stringLabel();
    void stringLineEdit_data() { addRendererRows(); }
    void stringLineEdit();
    void stringTextEdit_data() { addRendererRows(); }
    void stringTextEdit();
    void stringPasswordLineEdit_data() { addRendererRows(); }
    void stringPasswordLineEdit();
    void pathChooser_data() { addRendererRows(); }
    void pathChooser();
    void container_data() { addRendererRows(); }
    void container();
    void containerWithoutLayouter_data() { addRendererRows(); }
    void containerWithoutLayouter();
    void boolGroupChecker_data() { addRendererRows(); }
    void boolGroupChecker();
    void boolAdoptedButton_data() { addRendererRows(); }
    void boolAdoptedButton();
    void filePathValidity_data() { addRendererRows(); }
    void filePathValidity();
    void checkBoxForAnAspectThatIsNotBool_data() { addRendererRows(); }
    void comboBoxForAnAspectValuedByChoiceId_data() { addRendererRows(); }
    void comboBoxForAnAspectValuedByChoiceId();
    void containerThatReadsAsOneRow_data() { addRendererRows(); }
    void containerThatReadsAsOneRow();
    void checkBoxForAnAspectThatIsNotBool();
    void aspectThatDescribesNoControlDrawsNothing_data() { addRendererRows(); }
    void aspectThatDescribesNoControlDrawsNothing();
    void filePathLiveReconfiguration_data() { addRendererRows(); }
    void filePathLiveReconfiguration();
    void filePathFocusRequest_data() { addRendererRows(); }
    void filePathFocusRequest();
    void checkableStringLineEdit_data() { addRendererRows(); }
    void checkableStringLineEdit();
    void checkableFilePath_data() { addRendererRows(); }
    void checkableFilePath();
    void stringListTree_data() { addRendererRows(); }
    void stringListTree();
    void stringSelection_data() { addRendererRows(); }
    void stringSelection();
    void fontPicker_data() { addRendererRows(); }
    void fontPicker();
    void secretIsReadOnlyUntilItArrives_data() { addRendererRows(); }
    void secretIsReadOnlyUntilItArrives();
    void secretThatCannotBeReadStaysReadOnly_data() { addRendererRows(); }
    void secretThatCannotBeReadStaysReadOnly();
    void filePathListForAnAspectThatIsNotOne_data() { addRendererRows(); }
    void filePathListForAnAspectThatIsNotOne();
    void textWithAction_data() { addRendererRows(); }
    void textWithAction();
    void comboBoxFollowsARefill_data() { addRendererRows(); }
    void comboBoxFollowsARefill();
    void comboBoxKeepsAnIdAcrossARefill_data() { addRendererRows(); }
    void comboBoxKeepsAnIdAcrossARefill();
    void aHiddenAspectIsStillBuilt_data() { addRendererRows(); }
    void aHiddenAspectIsStillBuilt();
    void containerWithNoBoxOfItsOwn_data() { addRendererRows(); }
    void containerWithNoBoxOfItsOwn();
    void checkBoxWithALabelOfItsOwn_data() { addRendererRows(); }
    void checkBoxWithALabelOfItsOwn();
    void environmentChangesReadAsASummary_data() { addRendererRows(); }
    void environmentChangesReadAsASummary();
    void terminalCommandIsOneRow_data() { addRendererRows(); }
    void terminalCommandIsOneRow();
    void inlineRowLinesUpWithTheRowsAroundIt();
    void anInlineListItemThatReadsAsOneRowIsARow();
    void aspectListWithDetails_data() { addRendererRows(); }
    void aspectListWithDetails();
    void aspectInlineList_data() { addRendererRows(); }
    void aspectInlineList();
    void anAspectIsToldWhenItIsDrawn_data() { addRendererRows(); }
    void anAspectIsToldWhenItIsDrawn();
};

void tst_AspectRenderer::initTestCase()
{
    // The official entry point; individual tests switch the renderer through
    // setAspectRenderer() to also cover the fallback.
    Utils::installAspectWidgetRenderer();
}

// An aspect can ask to reach across more than one column, and the renderer
// wraps its control in a Layouting::Span to say so. Only a grid can honour
// that: a Row, a Column or a Flow places items as they arrive and has no
// columns to reach across, and an aspect cannot know which of those the page
// put it in.
//
// So the span is dropped there, and dropping it is correct. Saying so was
// not: the drop was reported with QTC_CHECK, which printed a soft assert
// seventy-two times in one run of ProjectExplorer's tests - and is fatal
// under QTC_FATAL_ASSERTS.
void tst_AspectRenderer::spanReachesAcrossColumnsAndIsQuietWhereThereAreNone()
{
    setRendererInstalled(true);

    IntegerAspect aspect;
    aspect.setLabelText("Width");
    aspect.setSpan(3);
    QCOMPARE(aspect.presentation().spanX, 3);

    // In a grid it reaches: the label takes the first column and the control
    // the two the aspect asked for beyond it.
    const std::unique_ptr<QWidget> grid(Layouting::Grid { aspect }.emerge());
    auto * const gridLayout = qobject_cast<QGridLayout *>(grid->layout());
    QVERIFY(gridLayout);
    auto * const spin = grid->findChild<QSpinBox *>();
    QVERIFY(spin);
    const int index = gridLayout->indexOf(spin);
    QVERIFY(index >= 0);
    int gridRow = 0, gridColumn = 0, rowSpan = 0, columnSpan = 0;
    gridLayout->getItemPosition(index, &gridRow, &gridColumn, &rowSpan, &columnSpan);
    QCOMPARE(columnSpan, 2);

    // In a column there is nothing to reach across, and nothing to say about
    // it. Asserted on the diagnostic rather than the drawing: the drop itself
    // is invisible, and what went wrong before was the complaint.
    QStringList complaints;
    QtMessageHandler previous = qInstallMessageHandler(nullptr);
    qInstallMessageHandler(previous);
    static QStringList *sink = nullptr;
    sink = &complaints;
    previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &m) {
        if (sink && m.contains("SOFT ASSERT"))
            *sink << m;
    });
    const std::unique_ptr<QWidget> inColumn = render(aspect);
    qInstallMessageHandler(previous);
    sink = nullptr;

    QVERIFY(inColumn->findChild<QSpinBox *>());
    QVERIFY2(complaints.isEmpty(), qPrintable("\n" + complaints.join("\n")));
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

// A GroupedListAspect is a view rather than a control, so it is put in whole
// instead of being built from the descriptor. A page that has not moved to Qt
// Quick still has to get a tree and the buttons that act on it.
void tst_AspectRenderer::groupedList()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    RendererToolsModel model;
    GroupedListAspect aspect;
    aspect.setModel(&model);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto tree = widget->findChild<QTreeView *>();
    QVERIFY(tree);
    QCOMPARE(tree->model(), model.groupedDisplayModel());
    // The groups, with the items under them.
    QCOMPARE(tree->model()->rowCount({}), 2);

    const QList<QPushButton *> buttons = widget->findChildren<QPushButton *>();
    QCOMPARE(buttons.size(), 3);
    // Nothing is current, so none of them does anything.
    QVERIFY(Utils::allOf(buttons, [](QPushButton *b) { return !b->isEnabled(); }));

    // The aspect and the view agree on what is current, whichever set it.
    aspect.setCurrentRow(1);
    QCOMPARE(model.mapToSource(tree->selectionModel()->currentIndex()).row(), 1);
    QVERIFY(Utils::anyOf(buttons, [](QPushButton *b) { return b->isEnabled(); }));
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
    aspect.setIconType(InfoType::Error);
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

    chooser->lineEdit()->insert("/tmp/one");
    QCOMPARE(aspect.volatileValue(), QString("/tmp/one"));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QVERIFY(aspect.volatileValue().isEmpty());
    QVERIFY(chooser->lineEdit()->text().isEmpty());

    aspect.setVolatileValue("/tmp/two");
    QCOMPARE(chooser->lineEdit()->text(), QString("/tmp/two"));
}

void tst_AspectRenderer::container()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    AspectContainer container;
    BoolAspect flag(&container);
    flag.setLabelText("Flag");
    StringAspect name(&container);
    name.setDisplayStyle(StringAspect::LineEditDisplay);
    name.setLabelText("Name");

    AspectWidgets::setLayouter(&container, [&flag, &name] {
        using namespace Layouting;
        return Form { flag, br, name };
    });

    const std::unique_ptr<QWidget> widget = render(container);
    QVERIFY(widget->findChild<QCheckBox *>());
    QVERIFY(widget->findChild<FancyLineEdit *>());
}

void tst_AspectRenderer::containerWithoutLayouter()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    // Used to call an empty std::function. Renders nothing now.
    AspectContainer container;
    const std::unique_ptr<QWidget> widget = render(container);
    QVERIFY(widget);
    QVERIFY(!widget->findChild<QCheckBox *>());
}

void tst_AspectRenderer::boolGroupChecker()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    BoolAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setValue(true);

    using namespace Layouting;
    const std::unique_ptr<QWidget> widget(
        Column {
            Group {
                title("Options"),
                groupChecker(AspectWidgets::groupChecker(&aspect)),
                Column { st },
            }
        }.emerge());

    auto groupBox = widget->findChild<QGroupBox *>();
    QVERIFY(groupBox);
    QVERIFY(groupBox->isCheckable());
    QVERIFY(groupBox->isChecked());

    // The group box is the aspect's check box, undo included. QGroupBox has no
    // click(); a real click sets the state and then emits clicked().
    groupBox->setChecked(false);
    emit groupBox->clicked(false);
    QCOMPARE(aspect.volatileValue(), false);
    QCOMPARE(stack.count(), 1);
    stack.undo();
    QCOMPARE(aspect.volatileValue(), true);
    QVERIFY(groupBox->isChecked());

    aspect.setVolatileValue(false);
    QVERIFY(!groupBox->isChecked());
}

void tst_AspectRenderer::boolAdoptedButton()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    QUndoStack stack;
    BoolAspect aspect;
    aspect.setAutoApply(false);
    aspect.setUndoStack(&stack);
    aspect.setLabel("Use the preset", BoolAspect::LabelPlacement::Compact);

    // A button the page owns, not one the aspect made.
    auto adopted = new QRadioButton;

    using namespace Layouting;
    const std::unique_ptr<QWidget> widget(
        Column { AspectWidgets::adoptButton(&aspect, adopted) }.emerge());

    QCOMPARE(adopted->text(), QString("Use the preset"));
    QVERIFY(!adopted->isChecked());

    adopted->click();
    QCOMPARE(aspect.volatileValue(), true);
    QCOMPARE(stack.count(), 1);
    stack.undo();
    QVERIFY(!adopted->isChecked());
}

void tst_AspectRenderer::filePathValidity()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    FilePathAspect aspect;
    aspect.setAutoApply(false);
    aspect.setExpectedKind(PathChooserKind::ExistingDirectory);

    // Nothing has been validated yet, and an empty path is not a directory.
    QVERIFY(!aspect.isValid());

    QSignalSpy validSpy(&aspect, &FilePathAspect::validChanged);
    const std::unique_ptr<QWidget> widget = render(aspect);
    auto chooser = widget->findChild<PathChooser *>();
    QVERIFY(chooser);

    chooser->lineEdit()->insert(QDir::tempPath());
    QTRY_VERIFY(aspect.isValid());
    QCOMPARE(chooser->isValid(), aspect.isValid());
    QCOMPARE(validSpy.count(), 1);
    QCOMPARE(validSpy.last().first().toBool(), true);

    chooser->lineEdit()->insert("/no-such-thing");
    QTRY_VERIFY(!aspect.isValid());
    QCOMPARE(validSpy.count(), 2);
    QCOMPARE(validSpy.last().first().toBool(), false);
}

// An aspect that asks for a check box without being a BoolAspect. This is
// TerminalAspect's shape: it holds a bool of its own plus "did the user set
// it", so it cannot be a TypedAspect<bool>, and it hands its value out as a
// variant. The renderer used to require the type and drew nothing for it.
class CheckBoxWithoutBoolAspect final : public BaseAspect
{
public:
    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::CheckBox;
        p.labelText = "Run in terminal";
        p.labelPlacement = AspectControls::LabelPlacement::AtControl;
        return p;
    }

    QVariant variantValue() const override { return m_on; }
    void setVariantValue(const QVariant &value, Announcement = DoEmit) override
    {
        m_on = value.toBool();
        emit volatileValueChanged();
    }
    QVariant volatileVariantValue() const override { return variantValue(); }
    void setVolatileVariantValue(const QVariant &value, Announcement a = DoEmit) override
    {
        setVariantValue(value, a);
    }

    bool m_on = false;
};

void tst_AspectRenderer::checkBoxForAnAspectThatIsNotBool()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    CheckBoxWithoutBoolAspect aspect;
    aspect.setVariantValue(true);

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    auto box = widget->findChild<QCheckBox *>();
    if (!withRenderer) {
        // Without a renderer nothing is built, and the aspect has no layout of
        // its own to fall back on any more.
        QVERIFY(!box);
        return;
    }

    QVERIFY(box);
    QCOMPARE(box->text(), QString("Run in terminal"));
    // What it shows is what the aspect holds, read as a variant.
    QVERIFY(box->isChecked());

    // Clicking writes back through the same variant.
    box->click();
    QVERIFY(!aspect.m_on);
    QVERIFY(!box->isChecked());

    // And a value set from elsewhere - a terminal mode changed on the
    // preferences page - reaches the box, which is what the aspect's own
    // pointer to it used to do.
    aspect.setVariantValue(true);
    QVERIFY(box->isChecked());
}

// An aspect whose value is the chosen entry's own id rather than its place in
// the list. This is LauncherAspect's shape: the entries are refilled whenever
// the device's launchers change, so a position means nothing across a refill.
class ComboWithIdValueAspect final : public BaseAspect
{
public:
    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::ComboBox;
        p.labelText = "Launcher:";
        p.valueIsChoiceId = true;
        p.minimumContentsLength = 12;
        for (const QString &id : m_ids)
            p.choices.append({id.toUpper(), {}, true, id});
        return p;
    }

    QVariant variantValue() const override { return m_current; }
    void setVariantValue(const QVariant &value, Announcement = DoEmit) override
    {
        m_current = value.toString();
        emit volatileValueChanged();
    }
    QVariant volatileVariantValue() const override { return variantValue(); }
    void setVolatileVariantValue(const QVariant &value, Announcement a = DoEmit) override
    {
        setVariantValue(value, a);
    }

    // What a device does when its launchers change: the entries are replaced
    // and the aspect says so, without its value having changed.
    void refill(const QStringList &ids)
    {
        m_ids = ids;
        emit controlConfigurationChanged();
    }

    QStringList m_ids{"alpha", "beta", "gamma"};
    QString m_current;
};

void tst_AspectRenderer::comboBoxForAnAspectValuedByChoiceId()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    ComboWithIdValueAspect aspect;
    aspect.setVariantValue("beta");

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    auto combo = widget->findChild<QComboBox *>();
    if (!withRenderer) {
        QVERIFY(!combo);
        return;
    }

    QVERIFY(combo);
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->itemText(0), QString("ALPHA"));

    // How wide it may get is the descriptor's to say. A list of encodings is
    // the reason: their names are long enough that a combo sized to its
    // contents pushes the page wider than the screen.
    QCOMPARE(combo->sizeAdjustPolicy(), QComboBox::AdjustToMinimumContentsLengthWithIcon);
    QCOMPARE(combo->minimumContentsLength(), 12);

    // The value is an id, so the entry shown is the one whose id it is - not
    // the entry at index 1 by coincidence of both being "beta".
    QCOMPARE(combo->currentIndex(), 1);

    // Picking writes the id back, not the index.
    combo->setCurrentIndex(2);
    QCOMPARE(aspect.m_current, QString("gamma"));

    // And a value set from elsewhere reaches the combo.
    aspect.setVariantValue("alpha");
    QCOMPARE(combo->currentIndex(), 0);

    // An id that is not on offer selects nothing rather than the first entry,
    // which is what a stale launcher looks like.
    aspect.setVariantValue("delta");
    QCOMPARE(combo->currentIndex(), -1);
}

void tst_AspectRenderer::containerThatReadsAsOneRow()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    // Some settings are several controls and one answer: which user to run as,
    // and - only for "Other" - which name. setInlineRow() says so, and the
    // renderer draws the label once with the controls after it. Aspects that
    // used to write that row by hand can stop.
    AspectContainer row;
    row.setLabelText("Run as user:");
    row.setInlineRow(true);

    SelectionAspect which(&row);
    which.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    which.addOption("Default");
    which.addOption("Other");

    StringAspect name(&row);
    name.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);

    const std::unique_ptr<QWidget> widget = render(row);
    QVERIFY(widget);
    if (!withRenderer) {
        QVERIFY(widget->findChildren<QComboBox *>().isEmpty());
        return;
    }

    auto combo = widget->findChild<QComboBox *>();
    auto edit = widget->findChild<QLineEdit *>();
    QVERIFY(combo);
    QVERIFY(edit);
    QCOMPARE(combo->count(), 2);

    // One label for the row, not one per control: that is what "reads as one
    // answer" means. Counting labels would count the variable chooser's own,
    // so this asks for the row's and checks it is the only visible one on the
    // row itself.
    QLabel * const rowLabel = Utils::findOr(widget->findChildren<QLabel *>(), nullptr,
                                            [](QLabel *l) { return l->text() == "Run as user:"; });
    QVERIFY(rowLabel);

    // The controls are one row, in order. Asked of the layout rather than of
    // the geometry, which is all zeroes until the widget is shown - and
    // showing it needs a window this test has no business wanting.
    QHBoxLayout *rowLayout = nullptr;
    for (QHBoxLayout *candidate : widget->findChildren<QHBoxLayout *>()) {
        if (candidate->indexOf(combo) >= 0)
            rowLayout = candidate;
    }
    QVERIFY(rowLayout);
    QVERIFY(rowLayout->indexOf(edit) >= 0);
    QVERIFY(rowLayout->indexOf(combo) < rowLayout->indexOf(edit));

    // The label is not in it. It belongs where every other control's label
    // goes, so that the controls start at the field column rather than after
    // however wide this row's own label happens to be - see
    // inlineRowLinesUpWithTheRowsAroundIt().
    QCOMPARE(rowLayout->indexOf(rowLabel), -1);

    // A container that says nothing about its layout and has none installed
    // draws its aspects in order - which is what AspectItems does on the Quick
    // side, and what an aspect that wants two plain rows can rely on instead
    // of writing them.
    AspectContainer plain;
    BoolAspect first(&plain);
    first.setLabel("First");
    first.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    BoolAspect second(&plain);
    second.setLabel("Second");
    second.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    const std::unique_ptr<QWidget> stacked = render(plain);
    QVERIFY(stacked);
    if (!withRenderer)
        return;
    QStringList texts;
    for (QCheckBox *box : stacked->findChildren<QCheckBox *>())
        texts << box->text();
    QCOMPARE(texts, (QStringList{"First", "Second"}));
}

void tst_AspectRenderer::aspectThatDescribesNoControlDrawsNothing()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    // Every aspect that names a control is drawn by the renderer, which is
    // what BaseAspect::addToLayoutImpl() does now that no aspect overrides it.
    // Two answers mean "not this one": Invisible is a container whose contents
    // the page lays out itself, and Custom is an aspect that has not been
    // described and builds its own control.
    //
    // Neither draws anything either way - the renderer does not know them, so
    // it declines. What separates asking from not asking is the complaint:
    // declining is reported as "no renderer could draw this", which is a soft
    // assert in this build and a qFatal under QTC_FATAL_ASSERTS.
    ContainerAspect invisible;
    QCOMPARE(invisible.presentation().control, AspectControls::Invisible);

    IdAspect custom;
    QCOMPARE(custom.presentation().control, AspectControls::Custom);

    QStringList complaints;
    const auto previous = qInstallMessageHandler(nullptr);
    static QStringList *sink = nullptr;
    sink = &complaints;
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &msg) {
        if (sink && msg.contains("SOFT ASSERT"))
            *sink << msg;
    });
    const QScopeGuard restore([previous] {
        sink = nullptr;
        qInstallMessageHandler(previous);
    });

    for (BaseAspect *aspect : {static_cast<BaseAspect *>(&invisible),
                               static_cast<BaseAspect *>(&custom)}) {
        const std::unique_ptr<QWidget> widget(render(*aspect));
        QVERIFY(widget);
        QVERIFY2(widget->findChildren<QWidget *>().isEmpty(),
                 QString::fromLatin1(aspect->metaObject()->className()).toUtf8());
    }
    QVERIFY2(complaints.isEmpty(), qPrintable(complaints.join("\n")));

    // And an aspect that does name one is drawn, so the check above is not
    // just "the renderer never draws anything".
    BoolAspect described;
    described.setLabel("Something");
    const std::unique_ptr<QWidget> widget(render(described));
    QVERIFY(widget);
    if (withRenderer)
        QVERIFY(!widget->findChildren<QCheckBox *>().isEmpty());
    QVERIFY2(complaints.isEmpty(), qPrintable(complaints.join("\n")));
}

// A value the aspect does not hold: it is asked for and turns up later, which
// is SecretAspect's shape without its keychain. Fetching is a step this test
// takes by hand, so that the moment before the value arrives is a moment the
// test can look at.
class FetchedSecretAspect final : public BaseAspect
{
public:
    FetchedSecretAspect()
    {
        setSettingsKey("Test.Token");
        setLabelText("Token");
        // Nothing may be typed in before the secret is there, exactly as
        // SecretAspect starts out.
        setReadOnly(true);
    }

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Secret;
        p.objectName = stringFromKey(settingsKey()) + ".secret";
        p.placeholderText = m_error;
        if (m_storageUnavailable)
            p.infoType = AspectControls::InfoType::Warning;
        return p;
    }

    QString displayText() const override { return m_secret; }
    void requestDisplayText() override { m_wasAsked = true; }

    // What the keychain would call back with.
    void deliver(const QString &secret)
    {
        m_secret = secret;
        setReadOnly(false);
        emit displayTextChanged();
    }

    void fail(const QString &reason)
    {
        m_error = reason;
        emit placeholderTextChanged(reason);
        emit displayTextChanged();
    }

    QVariant volatileVariantValue() const override { return m_secret; }
    void setVolatileVariantValue(const QVariant &value, Announcement = DoEmit) override
    {
        m_secret = value.toString();
    }

    bool m_wasAsked = false;
    bool m_storageUnavailable = false;
    QString m_secret;
    QString m_error;
};

static ShowPasswordButton *findRevealButton(QWidget *widget)
{
    // ShowPasswordButton has no Q_OBJECT macro, so findChild cannot key on it.
    for (QAbstractButton *button : widget->findChildren<QAbstractButton *>()) {
        if (auto b = dynamic_cast<ShowPasswordButton *>(button))
            return b;
    }
    return nullptr;
}

void tst_AspectRenderer::secretIsReadOnlyUntilItArrives()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    FetchedSecretAspect aspect;

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    auto field = widget->findChild<FancyLineEdit *>("Test.Token.secret");
    if (!withRenderer) {
        // Without a renderer nothing is built: the aspect has no layout of its
        // own any more.
        QVERIFY(!field);
        return;
    }

    QVERIFY(field);
    QCOMPARE(field->echoMode(), QLineEdit::Password);

    // Asked for as soon as there is somewhere to put it.
    QVERIFY(aspect.m_wasAsked);

    // ... and until it turns up, nothing may be typed over it. An empty field
    // is not evidence: an empty secret is a legitimate answer.
    QVERIFY(field->isReadOnly());
    ShowPasswordButton *reveal = findRevealButton(widget.get());
    QVERIFY(reveal);
    QVERIFY(!reveal->isEnabled());

    aspect.deliver("hunter2");
    QCOMPARE(field->text(), QString("hunter2"));
    QVERIFY(!field->isReadOnly());
    QVERIFY(reveal->isEnabled());

    // The reveal shows it, and typing writes back.
    reveal->click();
    QCOMPARE(field->echoMode(), QLineEdit::Normal);
    field->setText("s3cret");
    QCOMPARE(aspect.volatileVariantValue().toString(), QString("s3cret"));
}

void tst_AspectRenderer::secretThatCannotBeReadStaysReadOnly()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    FetchedSecretAspect aspect;
    aspect.m_storageUnavailable = true;
    aspect.setToolTip("No secret storage");

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    auto field = widget->findChild<FancyLineEdit *>("Test.Token.secret");
    if (!withRenderer) {
        QVERIFY(!field);
        return;
    }
    QVERIFY(field);

    // The warning is a label beside the field, and what it says is the
    // aspect's tool tip.
    QLabel *warning = nullptr;
    for (QLabel * const label : widget->findChildren<QLabel *>()) {
        if (!label->pixmap().isNull())
            warning = label;
    }
    QVERIFY(warning);
    QCOMPARE(warning->toolTip(), QString("No secret storage"));

    // A secret that could not be read says why, and stays read-only: typing
    // into it would store nothing over a secret that is still there.
    aspect.fail("Keychain refused");
    QCOMPARE(field->placeholderText(), QString("Keychain refused"));
    QVERIFY(field->isReadOnly());
    QVERIFY(!findRevealButton(widget.get())->isEnabled());
}

// A list of paths held by something that is not a FilePathListAspect, and that
// asks for files rather than directories. This is SuppressionAspect's shape:
// valgrind's suppression files are FilePaths, not strings, and the aspect
// stores them through its own variant conversion.
class PathsWithoutFilePathListAspect final : public BaseAspect
{
public:
    PathsWithoutFilePathListAspect()
    {
        setLabelText("Suppression files:");
    }

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::FilePathList;
        p.labelText = labelText();
        p.pathKind = AspectControls::PathKind::File;
        p.promptDialogTitle = "Valgrind Suppression Files";
        p.promptDialogFilter = "Valgrind Suppression File (*.supp)";
        return p;
    }

    QVariant variantValue() const override { return m_paths.toSettings(); }
    void setVariantValue(const QVariant &value, Announcement = DoEmit) override
    {
        m_paths = FilePaths::fromSettings(value);
        emit volatileValueChanged();
    }
    QVariant volatileVariantValue() const override { return variantValue(); }
    void setVolatileVariantValue(const QVariant &value, Announcement a = DoEmit) override
    {
        setVariantValue(value, a);
    }

    FilePaths m_paths;
};

void tst_AspectRenderer::filePathListForAnAspectThatIsNotOne()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    PathsWithoutFilePathListAspect aspect;

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    auto editor = widget->findChild<PathListEditor *>();
    if (!withRenderer) {
        // Without a renderer nothing is built: the aspect has no layout of its
        // own any more.
        QVERIFY(!editor);
        return;
    }

    QVERIFY(editor);

    // The label is the descriptor's, drawn beside the editor. It used to be
    // whatever the aspect's closure put there, which is why the widget backend
    // showed none and Qt Quick did.
    QLabel *label = widget->findChild<QLabel *>();
    QVERIFY(label);
    QCOMPARE(label->text(), QString("Suppression files:"));

    // What it holds is read and written as a variant, so the aspect need not
    // be a FilePathListAspect.
    editor->setPathList(QStringList("/tmp/one.supp")); // Fires changed(), like typing.
    QCOMPARE(aspect.m_paths, FilePaths{FilePath::fromString("/tmp/one.supp")});

    aspect.setVariantValue(FilePaths{FilePath::fromString("/tmp/two.supp")}.toSettings());
    QCOMPARE(editor->pathList(), QStringList("/tmp/two.supp"));

    // Insert... asks for the files the aspect named, not for a directory,
    // which is what a list of search paths would want.
    QCOMPARE(editor->fileDialogFilter(), QString("Valgrind Suppression File (*.supp)"));
    QCOMPARE(editor->fileDialogTitle(), QString("Valgrind Suppression Files"));
}

// A value edited through a dialog rather than in place: the control is a
// summary of it and a button that opens the dialog. Four aspects share this
// shape - MIME types, clangd's diagnostic configuration, the environment
// variable separators and the environment changes - and none of them can be
// asked to open a modal dialog from a test, so what is tested here is
// everything around it.
class SummaryWithButtonAspect final : public BaseAspect
{
public:
    SummaryWithButtonAspect() { setLabelText("MIME types:"); }

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::TextWithAction;
        p.labelText = labelText();
        p.actionText = "Set MIME Types...";
        return p;
    }

    QString displayText() const override { return m_types.join(';'); }
    void requestDisplayText() override { m_wasAsked = true; }
    void triggerAction() override
    {
        // Where the dialog would have been.
        m_types = QStringList{"text/x-c++src"};
        emit displayTextChanged();
    }

    bool m_wasAsked = false;
    QStringList m_types;
};

void tst_AspectRenderer::textWithAction()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    SummaryWithButtonAspect aspect;
    aspect.m_types = QStringList{"text/plain", "text/html"};

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    auto summary = widget->findChild<ElidingLabel *>();
    if (!withRenderer) {
        QVERIFY(!summary);
        return;
    }

    QVERIFY(summary);
    // Elided rather than wrapped: a list of MIME types is as long as it is,
    // and the row it sits in is one row.
    QCOMPARE(summary->text(), QString("text/plain;text/html"));

    // Asked as soon as there is somewhere to put it, for the aspects whose
    // summary has to be fetched.
    QVERIFY(aspect.m_wasAsked);

    QPushButton *button = nullptr;
    for (QPushButton * const b : widget->findChildren<QPushButton *>()) {
        if (b->text() == "Set MIME Types...")
            button = b;
    }
    QVERIFY(button);

    // The button does whatever the aspect says it does, and what comes back
    // reaches the summary without the aspect holding a pointer to it.
    button->click();
    QCOMPARE(summary->text(), QString("text/x-c++src"));
}

void tst_AspectRenderer::comboBoxFollowsARefill()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    SelectionAspect aspect;
    aspect.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    aspect.setLabelText("Device type:");
    aspect.addOption("first");
    aspect.addOption("second");
    aspect.setValue(1);

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    auto combo = widget->findChild<QComboBox *>();
    if (!withRenderer) {
        QVERIFY(!combo);
        return;
    }

    QVERIFY(combo);
    QCOMPARE(combo->count(), 2);
    QCOMPARE(combo->currentIndex(), 1);

    // Refilled while the page is open, which is what a list of device types or
    // toolchain ABIs does. The entries used to be read once and captured, so
    // the control went on showing a list that was gone.
    aspect.clearOptions();
    aspect.addOption("alpha");
    aspect.addOption("beta");
    aspect.addOption("gamma");

    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->itemText(0), QString("alpha"));
    // And the refill is not an edit: clearing a combo moves its current index,
    // which must not be written back as though the user had picked it.
    QCOMPARE(aspect.volatileValue(), 1);
    QCOMPARE(combo->currentIndex(), 1);
}

void tst_AspectRenderer::comboBoxKeepsAnIdAcrossARefill()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    ComboWithIdValueAspect aspect;
    aspect.setVariantValue("beta");

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    auto combo = widget->findChild<QComboBox *>();
    if (!withRenderer) {
        QVERIFY(!combo);
        return;
    }
    QVERIFY(combo);
    QCOMPARE(combo->currentIndex(), 1);

    // The same entry is still on offer, in a different place. Nothing the
    // aspect holds has changed, so only the refill can move the control - and
    // what it must follow is the id, not the position.
    aspect.refill({"gamma", "delta", "beta"});
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->currentIndex(), 2);
    QCOMPARE(aspect.m_current, QString("beta"));

    // Picking after a refill writes the id at that place in the list as it is
    // now. Reading it off the list as it was when the control was built - the
    // entries having been captured once - stores something else entirely.
    combo->setCurrentIndex(0);
    QCOMPARE(aspect.m_current, QString("gamma"));

    // An entry that went away selects nothing rather than whatever moved into
    // its place.
    aspect.setVariantValue("beta");
    aspect.refill({"gamma", "delta"});
    QCOMPARE(combo->currentIndex(), -1);
    QCOMPARE(aspect.m_current, QString("beta"));
}

void tst_AspectRenderer::aHiddenAspectIsStillBuilt()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    // A row that starts hidden and shows itself later - a warning under the
    // build directory, the Qt Quick compiler row when the kit changes. The
    // control has to exist all along: a page that only laid out the aspects
    // that were visible when it opened could never bring one back.
    TextDisplay warning(nullptr, "Build directory contains a space.");
    warning.setIconType(InfoType::Warning);
    warning.setVisible(false);

    const std::unique_ptr<QWidget> widget = render(warning);
    QVERIFY(widget);
    // InfoLabel has no Q_OBJECT macro, so findChild cannot key on it.
    InfoLabel *label = nullptr;
    for (QLabel * const candidate : widget->findChildren<QLabel *>()) {
        if (auto info = dynamic_cast<InfoLabel *>(candidate))
            label = info;
    }
    if (!withRenderer) {
        QVERIFY(!label);
        return;
    }

    QVERIFY(label);
    QCOMPARE(label->text(), QString("Build directory contains a space."));
    // isVisibleTo(), not isHidden(): nothing here is shown, so isHidden() is
    // true for every widget in the tree and asserts nothing.
    QVERIFY(!label->isVisibleTo(widget.get()));

    // And showing the aspect shows it, without anything having been laid out
    // again.
    warning.setVisible(true);
    QVERIFY(label->isVisibleTo(widget.get()));

    warning.setVisible(false);
    QVERIFY(!label->isVisibleTo(widget.get()));
}

void tst_AspectRenderer::containerWithNoBoxOfItsOwn()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    // An executable and the alternative to it on a device: one thing to
    // whatever runs them, so one aspect, but two rows of the page around them
    // rather than a group of two settings.
    const auto build = [](AspectContainer &page, bool flattened) {
        auto executable = new AspectContainer(&page);
        executable->setFlattened(flattened);
        for (const char *name : {"Executable:", "Alternate executable on device:"}) {
            auto path = new StringAspect(executable);
            path->setLabelText(QString::fromLatin1(name));
            path->setDisplayStyle(StringAspect::LineEditDisplay);
        }
        auto after = new StringAspect(&page);
        after->setLabelText("Arguments:");
        after->setDisplayStyle(StringAspect::LineEditDisplay);

        // A form, as a build or run panel is: a label column and a field
        // column, which is what there is to line up with in the first place.
        AspectWidgets::setLayouter(&page, [&page] {
            Layouting::Form form;
            for (BaseAspect * const aspect : page.aspects()) {
                form.addItem(aspect);
                form.addItem(Layouting::br);
            }
            return form;
        });
        return executable;
    };

    // The labels a page shows, and what each one was put in. A field is no use
    // here: FancyLineEdit holds another FancyLineEdit, so counting them counts
    // everything twice.
    const QStringList wanted{"Executable:", "Alternate executable on device:", "Arguments:"};
    const auto rows = [&wanted](QWidget *widget) {
        QList<QPair<QString, QWidget *>> found;
        // Only the labels this test asked for: a line edit brings its own -
        // the macro expander's "Select a variable to insert."
        for (QLabel * const label : widget->findChildren<QLabel *>()) {
            if (wanted.contains(label->text()))
                found.append({label->text(), label->parentWidget()});
        }
        return found;
    };

    AspectContainer flatPage;
    build(flatPage, true);
    const std::unique_ptr<QWidget> flat = render(flatPage);
    QVERIFY(flat);

    AspectContainer boxedPage;
    build(boxedPage, false);
    const std::unique_ptr<QWidget> boxed = render(boxedPage);
    QVERIFY(boxed);

    const QList<QPair<QString, QWidget *>> flatRows = rows(flat.get());
    if (!withRenderer) {
        QVERIFY(flatRows.isEmpty());
        return;
    }

    // In order, and all three in the same widget: the two inside the container
    // are rows of the page, not of a box of their own.
    QCOMPARE(Utils::transform(flatRows, &QPair<QString, QWidget *>::first), wanted);
    QCOMPARE(flatRows.at(1).second, flatRows.at(0).second);
    QCOMPARE(flatRows.at(2).second, flatRows.at(0).second);

    // Three rows of the form, not one row holding two of them: what the
    // container lists are settings in their own right, they just belong
    // together.
    auto form = flat->findChild<QFormLayout *>();
    QVERIFY(form);
    QCOMPARE(form->rowCount(), 3);

    // Without it, the container is a widget of its own and its rows are not
    // the page's - which is what every other nested container wants, and what
    // made this worth saying.
    const QList<QPair<QString, QWidget *>> boxedRows = rows(boxed.get());
    QCOMPARE(boxedRows.size(), 3);
    QCOMPARE(boxedRows.at(1).second, boxedRows.at(0).second);
    QVERIFY(boxedRows.at(2).second != boxedRows.at(0).second);

    // A container that was given a layout of its own means it: saying both is
    // saying the layout, because there is nowhere else for it to go.
    AspectContainer bothPage;
    AspectContainer * const both = build(bothPage, true);
    AspectWidgets::setLayouter(both, [both] {
        Layouting::Column column;
        for (BaseAspect * const aspect : both->aspects())
            column.addItem(aspect);
        return column;
    });
    const std::unique_ptr<QWidget> withLayout = render(bothPage);
    QVERIFY(withLayout);
    const QList<QPair<QString, QWidget *>> bothRows = rows(withLayout.get());
    QCOMPARE(bothRows.size(), 3);
    QVERIFY(bothRows.at(2).second != bothRows.at(0).second);
}

// A check box whose label is a link: "Use <a>global settings</a>" takes you to
// the page those settings come from. A QCheckBox draws its own text and draws
// it plain, so the words have to be a label of their own.
class LinkedCheckBoxAspect final : public BoolAspect
{
public:
    LinkedCheckBoxAspect()
    {
        setLabel("Use <a href=\"page\">global settings</a>",
                 BoolAspect::LabelPlacement::BesideCheckBox);
    }

    void activateLink(const QString &link) override { m_followed = link; }

    QString m_followed;
};

void tst_AspectRenderer::checkBoxWithALabelOfItsOwn()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    LinkedCheckBoxAspect aspect;
    aspect.setValue(true);

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    auto box = widget->findChild<QCheckBox *>();
    if (!withRenderer) {
        QVERIFY(!box);
        return;
    }

    QVERIFY(box);
    // Nothing on the box: what would be its text is beside it instead, and a
    // box that drew it too would say it twice.
    QVERIFY2(box->text().isEmpty(), qPrintable(box->text()));
    QVERIFY(box->isChecked());

    QLabel *label = nullptr;
    for (QLabel * const candidate : widget->findChildren<QLabel *>()) {
        if (candidate->text().contains("global settings"))
            label = candidate;
    }
    QVERIFY(label);
    // The markup intact, and reachable with the mouse - a link that cannot be
    // clicked is just underlined text.
    QVERIFY(label->text().contains("<a href="));
    QVERIFY(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));

    // Following it is the aspect's business: the renderer holds a BaseAspect
    // and has no idea what a settings page is.
    emit label->linkActivated("page");
    QCOMPARE(aspect.m_followed, QString("page"));

    // And it is still a check box.
    box->click();
    QVERIFY(!aspect.volatileValue());
}

void tst_AspectRenderer::environmentChangesReadAsASummary()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    // The real aspect, not a stand-in: its closure built the same summary and
    // the same button that TextWithAction does, and what is worth checking is
    // that deleting it left the row saying the same thing.
    EnvironmentChangesAspect aspect;
    aspect.setLabelText("Environment:");
    // So that the volatile value and the applied one can differ: on a page
    // with Apply and Cancel the summary has to show what is about to be
    // applied, not what already was.
    aspect.setAutoApply(false);
    aspect.setValue(EnvironmentChanges(
        {EnvironmentItem("PATH", "/usr/bin"), EnvironmentItem("TERM", "dumb")}));

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    auto summary = widget->findChild<ElidingLabel *>();
    if (!withRenderer) {
        QVERIFY(!summary);
        return;
    }

    QVERIFY(summary);
    QCOMPARE(summary->text(), aspect.displayText());
    QVERIFY2(summary->text().contains("PATH"), qPrintable(summary->text()));
    // Elided rather than wrapped: a list of changes is as long as it is, and
    // the row it sits in is one row.
    QCOMPARE(summary->elideMode(), Qt::ElideRight);

    // The summary follows the value, which is what the closure's own
    // connection did - and it is the volatile one it follows.
    aspect.setVolatileValue(EnvironmentChanges({EnvironmentItem("LANG", "C")}));
    QCOMPARE(summary->text(), aspect.displayText());
    QVERIFY2(summary->text().contains("LANG"), qPrintable(summary->text()));
    QVERIFY2(!summary->text().contains("PATH"), qPrintable(summary->text()));
    QCOMPARE(aspect.value().itemsFromUser().size(), 2);

    // And the dialog is still one button away. Clicking it is not something a
    // test can do - it is modal - so this is as far as it goes.
    QPushButton *button = nullptr;
    for (QPushButton * const b : widget->findChildren<QPushButton *>()) {
        if (!b->text().isEmpty())
            button = b;
    }
    QVERIFY(button);
    QCOMPARE(button->text(), aspect.presentation().actionText);
}

void tst_AspectRenderer::terminalCommandIsOneRow()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    TerminalCommandAspect aspect(nullptr);
    aspect.setLabelText("Terminal:");

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);

    // Only what is on the page: the hidden dialog fields bring buttons of
    // their own along - a path chooser has a Browse.
    QStringList buttons;
    for (QPushButton * const b : widget->findChildren<QPushButton *>()) {
        if (!b->text().isEmpty() && b->isVisibleTo(widget.get()))
            buttons << b->text();
    }
    if (!withRenderer) {
        QVERIFY(buttons.isEmpty());
        return;
    }

    // One row: a summary of what the three fields come to, the button that
    // opens them and the menu of what this machine has.
    QCOMPARE(buttons.size(), 2);
    QVERIFY2(buttons.contains(aspect.customize.presentation().actionText),
             qPrintable(buttons.join(" | ")));
    QVERIFY2(buttons.contains(aspect.presets.presentation().actionText),
             qPrintable(buttons.join(" | ")));

    // The fields themselves are the dialog's. They are in a container that is
    // hidden here - hidden, not absent, because the dialog draws them and a
    // hidden aspect would be hidden there too.
    QVERIFY(!aspect.command.isVisible());
    QVERIFY(aspect.terminalEmulator.isVisible());
    for (QLabel * const label : widget->findChildren<QLabel *>()) {
        if (label->text() == aspect.terminalOpenArgs.labelText())
            QVERIFY2(!label->isVisibleTo(widget.get()), "a dialog field is on the page");
    }

    // The summary is derived from all three, so none of them knows on its own
    // that it has changed.
    auto summary = widget->findChild<ElidingLabel *>();
    QVERIFY(summary);

    // And it is one row: the summary and both buttons sit in the same
    // horizontal layout, rather than a row each under one another.
    bool foundRow = false;
    for (QHBoxLayout * const row : widget->findChildren<QHBoxLayout *>()) {
        if (row->indexOf(summary) < 0)
            continue;
        int found = 0;
        for (QPushButton * const b : widget->findChildren<QPushButton *>()) {
            if (buttons.contains(b->text()) && row->indexOf(b) >= 0)
                ++found;
        }
        if (found == 2)
            foundRow = true;
    }
    QVERIFY2(foundRow, "the summary and its buttons are not in one row");
    aspect.terminalOpenArgs.setVolatileValue("--from-the-test");
    QVERIFY2(summary->text().contains("--from-the-test"), qPrintable(summary->text()));

    // A preset is one choice that sets all three at once, which is the whole
    // reason the menu is there.
    const QList<AspectPresentation::Choice> choices = aspect.presets.presentation().choices;
    if (choices.isEmpty())
        QSKIP("This machine reports no terminal emulators to choose between");
    aspect.presets.triggerChoice(choices.last().id);
    QCOMPARE(FilePath::fromUserInput(aspect.terminalEmulator.volatileValue()).toUserOutput(),
             choices.last().display);
    QVERIFY(!summary->text().contains("--from-the-test"));
}

void tst_AspectRenderer::anInlineListItemThatReadsAsOneRowIsARow()
{
    setRendererInstalled(true);

    // An inline list draws each item by adding it to a row, which asks the item
    // how it draws itself. An item that reads as one value - a port mapping is
    // four fields that mean one mapping - used to answer with a layouter of its
    // own, which only this backend could read. The descriptor says it instead;
    // QuickUiTest::testAnInlineListItemCanReadAsOneRow() asks the other one.
    const auto fieldPositions = [](AspectContainer &page,
                                   bool inRow,
                                   std::unique_ptr<QWidget> &keep) {
        auto list = new AspectList(&page);
        list->setLabelText("Port mappings:");
        list->setCreateItemFunction([inRow] {
            auto item = std::make_shared<AspectContainer>();
            item->setInlineRow(inRow);
            for (const char *n : {"HostPort", "ContainerPort"}) {
                auto part = new StringAspect(item.get());
                part->setLabelText(QString::fromLatin1(n));
                part->setDisplayStyle(StringAspect::LineEditDisplay);
            }
            return item;
        });
        list->createAndAddItem();

        keep = render(*list);
        keep->resize(800, 300);
        keep->show();
        // QVERIFY returns void, and this one has positions to hand back.
        if (!QTest::qVerify(QTest::qWaitForWindowExposed(keep.get()),
                            "QTest::qWaitForWindowExposed(keep.get())", "",
                            __FILE__, __LINE__)) {
            return QList<QPoint>{};
        }

        // Distinct positions rather than a count: the list replaces its layout
        // when its items change and the widgets it replaced are deleted later,
        // so the same field is found twice. Where they are is the question
        // anyway.
        QList<QPoint> seen;
        for (QLineEdit * const field : keep->findChildren<QLineEdit *>()) {
            const QPoint at = field->mapTo(keep.get(), QPoint(0, 0));
            if (!seen.contains(at))
                seen.append(at);
        }
        std::sort(seen.begin(), seen.end(), [](QPoint a, QPoint b) {
            return a.y() != b.y() ? a.y() < b.y() : a.x() < b.x();
        });
        return seen;
    };

    AspectContainer stackedPage;
    std::unique_ptr<QWidget> stackedForm;
    const QList<QPoint> stacked = fieldPositions(stackedPage, false, stackedForm);
    QCOMPARE(stacked.size(), 2);
    QCOMPARE(stacked.at(1).x(), stacked.at(0).x());
    QVERIFY(stacked.at(1).y() > stacked.at(0).y());

    AspectContainer rowPage;
    std::unique_ptr<QWidget> rowForm;
    const QList<QPoint> row = fieldPositions(rowPage, true, rowForm);
    QCOMPARE(row.size(), 2);
    QCOMPARE(row.at(1).y(), row.at(0).y());
    QVERIFY(row.at(1).x() > row.at(0).x());
}

void tst_AspectRenderer::inlineRowLinesUpWithTheRowsAroundIt()
{
    setRendererInstalled(true);

    // A form: a label column and a field column. A row that reads as one
    // answer is still a row of that form, so its label belongs in the label
    // column like everybody else's. It used to go in whole as a spanning item,
    // which put its controls after its own label's width instead of at the
    // field column - visible as soon as an ABI row sat under a Name row.
    AspectContainer page;
    auto plain = new StringAspect(&page);
    plain->setLabelText("Name:");
    plain->setDisplayStyle(StringAspect::LineEditDisplay);

    auto abi = new AspectContainer(&page);
    abi->setLabelText("ABI:");
    abi->setInlineRow(true);
    for (const char *n : {"Arch", "Os"}) {
        auto part = new SelectionAspect(abi);
        part->setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        part->addOption(QString::fromLatin1(n));
    }

    AspectWidgets::setLayouter(&page, [&page] {
        Layouting::Form form;
        for (BaseAspect * const a : page.aspects()) {
            form.addItem(a);
            form.addItem(Layouting::br);
        }
        return form;
    });

    const std::unique_ptr<QWidget> widget = render(page);
    QVERIFY(widget);
    auto form = widget->findChild<QFormLayout *>();
    QVERIFY(form);
    QCOMPARE(form->rowCount(), 2);

    const auto labelAt = [form](int row) {
        QLayoutItem * const item = form->itemAt(row, QFormLayout::LabelRole);
        if (!item || !item->widget())
            return QString();
        auto label = qobject_cast<QLabel *>(item->widget());
        return label ? label->text() : QString();
    };

    QCOMPARE(labelAt(0), QString("Name:"));
    // The one that used to be missing: a spanning item has no label item at
    // all, so this reads as an empty string.
    QCOMPARE(labelAt(1), QString("ABI:"));
    QVERIFY(form->itemAt(1, QFormLayout::FieldRole));
    QVERIFY(!form->itemAt(1, QFormLayout::SpanningRole));

    // And what is in the field column is still the row of controls.
    QCOMPARE(widget->findChildren<QComboBox *>().size(), 2);
}

// The shape every AspectList has: a list of items, each of them an aspect that
// the page knows how to make one more of.
static void fillWithStrings(AspectList &list)
{
    list.setCreateItemFunction([] {
        auto item = std::make_shared<StringAspect>();
        item->setDisplayStyle(StringAspect::LineEditDisplay);
        item->setLabelText("Name:");
        item->setValue("new");
        return item;
    });
    list.listViewDataCallback = [](BaseAspect *item, int) -> QVariant {
        return static_cast<StringAspect *>(item)->value();
    };
}

static QPushButton *buttonNamed(QWidget *widget, const QString &text)
{
    for (QPushButton * const b : widget->findChildren<QPushButton *>()) {
        if (b->text() == text)
            return b;
    }
    return nullptr;
}

void tst_AspectRenderer::aspectListWithDetails()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    AspectList list;
    list.setDisplayStyle(AspectList::DisplayStyle::ListViewWithDetails);
    list.setOrdered(true);
    fillWithStrings(list);

    const std::unique_ptr<QWidget> widget = render(list);
    QVERIFY(widget);
    auto view = widget->findChild<QTreeView *>();
    if (!withRenderer) {
        QVERIFY(!view);
        return;
    }

    QVERIFY(view);
    QVERIFY(view->model());
    QCOMPARE(view->model()->rowCount({}), 0);

    QPushButton * const add = buttonNamed(widget.get(), "Add");
    QPushButton * const remove = buttonNamed(widget.get(), "Remove");
    QVERIFY(add);
    QVERIFY(remove);
    // Ordered, so it offers to reorder. A list whose order means nothing does
    // not - see setOrdered().
    QPushButton * const up = buttonNamed(widget.get(), "Move Up");
    QPushButton * const down = buttonNamed(widget.get(), "Move Down");
    QVERIFY(up);
    QVERIFY(down);
    QVERIFY(!up->isEnabled());
    QVERIFY(!down->isEnabled());

    // Adding makes the new item the current one, so that the details pane is
    // about what was just added.
    add->click();
    QCOMPARE(list.volatileItems().size(), 1);
    QCOMPARE(view->model()->rowCount({}), 1);
    QCOMPARE(list.currentIndex(), 0);
    // And the pane is the item's own controls, not a copy of them.
    QVERIFY(widget->findChild<FancyLineEdit *>());

    add->click();
    QCOMPARE(list.volatileItems().size(), 2);
    QCOMPARE(list.currentIndex(), 1);
    QVERIFY(up->isEnabled());
    QVERIFY(!down->isEnabled());

    // Which item is current is the aspect's answer: a move takes it with it.
    const std::shared_ptr<BaseAspect> second = list.volatileItems().at(1);
    up->click();
    QCOMPARE(list.currentIndex(), 0);
    QCOMPARE(list.volatileItems().at(0), second);

    // Removing takes the item out of the list but leaves the row, struck
    // through, until the page is applied: a removal is something to undo, not
    // something to do twice. The Qt Quick side has to draw the same thing,
    // which is why the model says so rather than the view.
    remove->click();
    QCOMPARE(list.volatileItems().size(), 1);
    QCOMPARE(view->model()->rowCount({}), 2);
    int struckThrough = 0;
    for (int row = 0; row < view->model()->rowCount({}); ++row) {
        const QFont font = view->model()->index(row, 0).data(Qt::FontRole).value<QFont>();
        if (font.strikeOut())
            ++struckThrough;
    }
    QCOMPARE(struckThrough, 1);
}

void tst_AspectRenderer::aspectInlineList()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    AspectList list;
    list.setDisplayStyle(AspectList::DisplayStyle::InlineList);
    fillWithStrings(list);
    list.createAndAddItem();
    list.createAndAddItem();

    const std::unique_ptr<QWidget> widget = render(list);
    QVERIFY(widget);
    const QList<FancyLineEdit *> fields = widget->findChildren<FancyLineEdit *>();
    if (!withRenderer) {
        QVERIFY(fields.isEmpty());
        return;
    }

    // Every item is drawn in full, rather than a list with a details pane
    // beside it: two items, two editors. A FancyLineEdit holds another one -
    // a grandchild, not a child - so count those with none above them.
    const auto isNested = [](QWidget *field) {
        for (QWidget *w = field->parentWidget(); w; w = w->parentWidget()) {
            if (qobject_cast<FancyLineEdit *>(w))
                return true;
        }
        return false;
    };
    int editors = 0;
    for (FancyLineEdit * const field : fields) {
        if (!isNested(field))
            ++editors;
    }
    QCOMPARE(editors, 2);

    // One remove beside each item, and one add at the end - and only those:
    // a line edit brings buttons of its own, the macro expander's among them.
    // Asked again after every change: the whole list is rebuilt when it
    // changes, so a button held from before is not the one on screen.
    const auto ownButtons = [&widget, &isNested] {
        QList<QAbstractButton *> found;
        for (QAbstractButton * const b : widget->findChildren<QAbstractButton *>()) {
            if (!isNested(b) && b->isVisibleTo(widget.get()))
                found << b;
        }
        return found;
    };
    QCOMPARE(ownButtons().size(), 3);

    // The last one adds; the rest take their item out.
    ownButtons().last()->click();
    QCOMPARE(list.volatileItems().size(), 3);
    QCOMPARE(ownButtons().size(), 4);

    ownButtons().first()->click();
    QCOMPARE(list.volatileItems().size(), 2);
}

// An aspect that counts being told it is on screen. Any control will do: the
// point is that the telling does not depend on which one.
class CountingShownAspect final : public BoolAspect
{
public:
    CountingShownAspect() { setLabel("Enabled", BoolAspect::LabelPlacement::Compact); }

    void requestDisplayText() override { ++shown; }

    int shown = 0;
};

void tst_AspectRenderer::anAspectIsToldWhenItIsDrawn()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    // Three controls used to do this and the rest did not, so an aspect whose
    // label reports something it has to go and look up worked as a button and
    // not as a check box. It is asked once, wherever it is drawn.
    CountingShownAspect aspect;
    QCOMPARE(aspect.shown, 0);

    const std::unique_ptr<QWidget> widget = render(aspect);
    QVERIFY(widget);
    if (!withRenderer) {
        QVERIFY(!widget->findChild<QCheckBox *>());
        QCOMPARE(aspect.shown, 0);
        return;
    }

    QVERIFY(widget->findChild<QCheckBox *>());
    QCOMPARE(aspect.shown, 1);

    // Drawn again is asked again: a page reopened has to look again too.
    const std::unique_ptr<QWidget> second = render(aspect);
    QVERIFY(second);
    QCOMPARE(aspect.shown, 2);
}

void tst_AspectRenderer::filePathLiveReconfiguration()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    FilePathAspect aspect;
    aspect.setAutoApply(false);
    aspect.setExpectedKind(PathChooserKind::File);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto chooser = widget->findChild<PathChooser *>();
    QVERIFY(chooser);
    QCOMPARE(chooser->expectedKind(), PathChooserKind::File);

    // The aspect does not know the widget; the setter has to reach it anyway.
    aspect.setExpectedKind(PathChooserKind::ExistingDirectory);
    QCOMPARE(chooser->expectedKind(), PathChooserKind::ExistingDirectory);

    aspect.setPromptDialogTitle("Pick one");
    QCOMPARE(chooser->promptDialogTitle(), QString("Pick one"));
}

void tst_AspectRenderer::filePathFocusRequest()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    FilePathAspect aspect;
    aspect.setAutoApply(false);

    // A second focusable widget, so that the aspect's control is not the only
    // candidate and getting focus means something.
    const auto window = std::make_unique<QWidget>();
    auto layout = new QVBoxLayout(window.get());
    auto other = new QLineEdit;
    layout->addWidget(other);
    layout->addWidget(Layouting::Column { aspect }.emerge());

    auto chooser = window->findChild<PathChooser *>();
    QVERIFY(chooser);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    // focusWidget(), not hasFocus(): the latter is application-wide and is
    // false whenever this window is not the active one - which, on a machine
    // with anything else open, is always. What the aspect promises is that
    // its field is where typing would go in this window, and that is what
    // QWidget::focusWidget() answers.
    other->setFocus();
    QCOMPARE(window->focusWidget(), other);

    aspect.setFocusToInputField();
    QWidget * const focused = window->focusWidget();
    QVERIFY(focused);
    QVERIFY2(focused == chooser || chooser->isAncestorOf(focused),
             qPrintable(QString("focus went to %1, not to the path chooser")
                            .arg(QString::fromLatin1(focused->metaObject()->className()))));
}

void tst_AspectRenderer::checkableStringLineEdit()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    StringAspect aspect;
    aspect.setAutoApply(false);
    aspect.setDisplayStyle(StringAspect::LineEditDisplay);
    aspect.setLabelText("Name");
    aspect.makeCheckable(CheckBoxPlacement::Right, "Override", "Override");

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto lineEdit = widget->findChild<FancyLineEdit *>();
    QVERIFY(lineEdit);
    auto checkBox = widget->findChild<QCheckBox *>();
    QVERIFY(checkBox);
    QCOMPARE(checkBox->text(), QString("Override"));

    // Unchecked greys out the control it guards, and its label.
    QVERIFY(!checkBox->isChecked());
    QVERIFY(!lineEdit->isEnabled());
    auto label = widget->findChild<QLabel *>();
    QVERIFY(label);
    QVERIFY(!label->isEnabled());

    checkBox->click();
    QVERIFY(aspect.isChecked());
    QVERIFY(lineEdit->isEnabled());
    QVERIFY(label->isEnabled());

    checkBox->click();
    QVERIFY(!aspect.isChecked());
    QVERIFY(!lineEdit->isEnabled());
    QVERIFY(!label->isEnabled());
}

void tst_AspectRenderer::checkableFilePath()
{
    QFETCH(bool, withRenderer);
    setRendererInstalled(withRenderer);

    FilePathAspect aspect;
    aspect.setAutoApply(false);
    aspect.setLabelText("Build directory");
    aspect.makeCheckable(CheckBoxPlacement::Top, "Shadow build:", "Shadow");

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto chooser = widget->findChild<PathChooser *>();
    QVERIFY(chooser);
    auto checkBox = widget->findChild<QCheckBox *>();
    QVERIFY(checkBox);

    QVERIFY(!chooser->isEnabled());
    checkBox->click();
    QVERIFY(chooser->isEnabled());
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
    aspect.setControlObjectName("theBox");
    aspect.setMinimumContentsLength(20);
    aspect.setSizeAdjustPolicy(AspectControls::SizeAdjustPolicy::ToContents);

    const std::unique_ptr<QWidget> widget = render(aspect);
    auto comboBox = widget->findChild<QComboBox *>();
    QVERIFY(comboBox);
    QCOMPARE(comboBox->objectName(), QString("theBox"));
    QCOMPARE(comboBox->minimumContentsLength(), 20);
    QCOMPARE(comboBox->sizeAdjustPolicy(), QComboBox::AdjustToContents);
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

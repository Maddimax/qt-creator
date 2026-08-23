// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectwidgetrenderer.h"

#include "aspectwidgets.h"
#include "aspects.h"
#include "checkableaspect.h"
#include "elidinglabel.h"
#include "environment.h"
#include "fancylineedit.h"
#include "guard.h"
#include "guiutils.h"
#include "infolabel.h"
#include "layoutbuilder.h"
#include "passworddialog.h"
#include "pathchooser.h"
#include "pathlisteditor.h"
#include "qtcassert.h"
#include "qtcolorbutton.h"
#include "stylehelper.h"
#include "utilstr.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDebug>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFontInfo>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Layouting;

namespace Utils::Internal {

const char BUTTONS_ADDED[] = "QtcAspect.ButtonsAdded";

// Rebuilds the widgets of the addToLayoutImpl() bodies in aspects.cpp. The
// generic controls are driven by presentation() alone; the bespoke ones key on
// the aspect type and read the rest through friend accessors - the point is
// where the widget code lives, not that it be generic. GUI writes go through
// setVolatileVariantValueFromGui(), which records undo like the bodies' direct
// UndoableValue writes; GUI reads follow volatileValueChanged(), or the
// aspect's UndoableValue signal where the body relies on its exact timing.
class AspectWidgetRenderer
{
public:
    static bool render(BaseAspect &aspect, Layout &parent)
    {
        const AspectPresentation pres = aspect.presentation();
        switch (pres.control) {
        case AspectControls::CheckBox:
        case AspectControls::RadioButton:
            if (auto boolAspect = qobject_cast<BoolAspect *>(&aspect)) {
                renderBool(boolAspect, parent, pres);
                return true;
            }
            return false;
        case AspectControls::FontFamilyPicker:
            if (auto fontFamilyAspect = qobject_cast<FontFamilyAspect *>(&aspect)) {
                renderFontFamily(fontFamilyAspect, parent, pres);
                return true;
            }
            return false;
        case AspectControls::ComboBox:
        case AspectControls::RadioButtonGroup:
            if (auto selectionAspect = qobject_cast<SelectionAspect *>(&aspect)) {
                renderSelection(selectionAspect, parent, pres);
                return true;
            }
            if (auto stringSelectionAspect = qobject_cast<StringSelectionAspect *>(&aspect)) {
                renderStringSelection(stringSelectionAspect, parent);
                return true;
            }
            return false;
        case AspectControls::MultiSelection:
            if (auto multiSelectionAspect = qobject_cast<MultiSelectionAspect *>(&aspect)) {
                renderMultiSelection(multiSelectionAspect, parent, pres);
                return true;
            }
            return false;
        case AspectControls::SpinBox:
            if (auto integerAspect = qobject_cast<IntegerAspect *>(&aspect)) {
                renderSpinBox(integerAspect, parent, pres);
                return true;
            }
            return false;
        case AspectControls::DoubleSpinBox:
            if (auto doubleAspect = qobject_cast<DoubleAspect *>(&aspect)) {
                renderDoubleSpinBox(doubleAspect, parent, pres);
                return true;
            }
            return false;
        case AspectControls::CommaSeparatedLineEdit:
            if (auto stringListAspect = qobject_cast<StringListAspect *>(&aspect)) {
                renderCommaSeparatedLineEdit(stringListAspect, parent);
                return true;
            }
            return false;
        case AspectControls::FilePathList:
            if (auto filePathListAspect = qobject_cast<FilePathListAspect *>(&aspect)) {
                renderFilePathList(filePathListAspect, parent, pres);
                return true;
            }
            return false;
        case AspectControls::IntegerList:
            // Renders nothing, like IntegersAspect's own body.
            return qobject_cast<IntegersAspect *>(&aspect) != nullptr;
        case AspectControls::Label:
            if (auto stringAspect = qobject_cast<StringAspect *>(&aspect)) {
                withChecker(stringAspect, parent, [&] {
                    renderStringLabel(stringAspect, parent, pres);
                });
                return true;
            }
            if (auto textDisplay = qobject_cast<TextDisplay *>(&aspect)) {
                renderTextDisplay(textDisplay, parent, pres);
                return true;
            }
            return false;
        case AspectControls::LineEdit:
        case AspectControls::PasswordLineEdit:
            if (auto stringAspect = qobject_cast<StringAspect *>(&aspect)) {
                withChecker(stringAspect, parent, [&] {
                    renderStringLineEdit(stringAspect, parent, pres);
                });
                return true;
            }
            return false;
        case AspectControls::TextEdit:
            if (auto stringAspect = qobject_cast<StringAspect *>(&aspect)) {
                withChecker(stringAspect, parent, [&] {
                    renderStringTextEdit(stringAspect, parent, pres);
                });
                return true;
            }
            return false;
        case AspectControls::PathChooser:
            if (auto filePathAspect = qobject_cast<FilePathAspect *>(&aspect)) {
                withChecker(filePathAspect, parent, [&] {
                    renderPathChooser(filePathAspect, parent, pres);
                });
                return true;
            }
            return false;
        case AspectControls::ColorPicker:
            if (auto colorAspect = qobject_cast<ColorAspect *>(&aspect)) {
                renderColor(colorAspect, parent, pres);
                return true;
            }
            return false;
        case AspectControls::StringList:
            if (auto stringListAspect = qobject_cast<StringListAspect *>(&aspect)) {
                renderStringListTree(stringListAspect, parent, pres);
                return true;
            }
            return false;
        case AspectControls::FontPicker:
            if (auto fontAspect = qobject_cast<FontAspect *>(&aspect)) {
                renderFontPicker(fontAspect, parent);
                return true;
            }
            return false;
        case AspectControls::Container:
            if (auto container = qobject_cast<AspectContainer *>(&aspect)) {
                if (const AspectWidgets::Layouter l = AspectWidgets::layouter(container))
                    parent.addItem(l());
                return true;
            }
            return false;
        // Not handled:
        default:
            return false;
        }
    }

private:
    // Everything a FilePathAspect's setters can change after the control
    // exists. Idempotent: it runs at build time and again on every
    // controlConfigurationChanged().
    static void applyPathChooserConfiguration(FilePathAspect *aspect, PathChooser *pathChooser)
    {
        pathChooser->setExpectedKind(aspect->expectedKind());
        if (!aspect->historyCompleterKey().isEmpty())
            pathChooser->setHistoryCompleter(aspect->historyCompleterKey());
        if (const std::optional<ValidationFunction> validator = aspect->validationFunction())
            pathChooser->setValidationFunction(*validator);
        pathChooser->setEnvironment(aspect->environment());
        pathChooser->setBaseDirectory(aspect->baseDirectory());
        pathChooser->setInitialBrowsePathBackup(aspect->initialBrowsePathBackup());
        pathChooser->setOpenTerminalHandler(aspect->openTerminalHandler());
        pathChooser->setPromptDialogFilter(aspect->promptDialogFilter());
        pathChooser->setPromptDialogTitle(aspect->promptDialogTitle());
        pathChooser->setCommandVersionArguments(aspect->commandVersionArguments());
        pathChooser->setAllowPathFromDevice(aspect->allowPathFromDevice());
        pathChooser->setReadOnly(aspect->isReadOnly());
        pathChooser->lineEdit()->setValidatePlaceHolder(aspect->validatePlaceHolder());
        pathChooser->setValueAlternatives(aspect->valueAlternatives());

        // addButton() has no counterpart, so only the ones not added yet.
        const QList<FilePathAspect::Button> buttons = aspect->buttons();
        const int added = pathChooser->property(BUTTONS_ADDED).toInt();
        for (int i = added; i < buttons.size(); ++i) {
            const FilePathAspect::Button &button = buttons.at(i);
            pathChooser->addButton(button.text, button.context, button.callback);
        }
        pathChooser->setProperty(BUTTONS_ADDED, buttons.size());
    }

    static void applyComboBoxSizing(QComboBox *comboBox, const AspectPresentation &pres)
    {
        comboBox->setSizeAdjustPolicy(
            pres.sizeAdjustPolicy == AspectControls::SizeAdjustPolicy::ToContents
                ? QComboBox::AdjustToContents
                : QComboBox::AdjustToMinimumContentsLengthWithIcon);
        if (pres.minimumContentsLength > 0)
            comboBox->setMinimumContentsLength(pres.minimumContentsLength);
    }

    // The check box, where the aspect has one, brackets its control.
    template<class Aspect, class Render>
    static void withChecker(Aspect *aspect, Layout &parent, const Render &render)
    {
        CheckableAspectImplementation &checker = aspect->checker();
        BoolAspect *checked = checker.m_checked.get();
        if (checked) {
            if (checker.m_checkBoxPlacement == CheckBoxPlacement::Top) {
                checked->addToLayoutImpl(parent);
                parent.flush();
            } else if (checker.m_checkBoxPlacement == CheckBoxPlacement::Left) {
                checked->addToLayoutImpl(parent);
            }
        }

        render();

        if (checked && checker.m_checkBoxPlacement == CheckBoxPlacement::Right)
            checked->addToLayoutImpl(parent);
    }

    static void setControlReadOnly(QLabel *, bool) {}

    template<class Widget>
    static void setControlReadOnly(Widget *w, bool readOnly)
    {
        w->setReadOnly(readOnly);
    }

    // What the optional check box does to the control it guards.
    template<class Aspect, class Widget>
    static void updateFromCheckStatus(Aspect *aspect, Widget *w)
    {
        const CheckableAspectImplementation &checker = aspect->checker();
        const bool enabled = !checker.m_checked || checker.m_checked->volatileValue();
        if (checker.m_uncheckedSemantics == UncheckedSemantics::Disabled)
            w->setEnabled(enabled && aspect->isEnabled());
        else
            setControlReadOnly(w, !enabled || aspect->isReadOnly());
    }

    // addLabeledItem(), plus the greying out the check box drives.
    template<class Aspect, class Widget>
    static void addCheckableLabeledItem(Aspect *aspect, Layout &parent, Widget *widget)
    {
        QLabel *label = AspectWidgets::addLabeledItem(aspect, parent, widget);
        updateFromCheckStatus(aspect, widget);
        if (label)
            updateFromCheckStatus(aspect, label);

        BoolAspect *checked = aspect->checker().m_checked.get();
        if (!checked)
            return;
        QObject::connect(checked, &BoolAspect::volatileValueChanged, widget,
                         [aspect, widget] { updateFromCheckStatus(aspect, widget); });
        if (label) {
            QObject::connect(checked, &BoolAspect::volatileValueChanged, label,
                             [aspect, label] { updateFromCheckStatus(aspect, label); });
        }
    }

    static void renderBool(BoolAspect *aspect, Layout &parent, const AspectPresentation &pres)
    {
        QAbstractButton *button = pres.control == AspectControls::RadioButton
                                      ? static_cast<QAbstractButton *>(
                                            AspectWidgets::createSubWidget<QRadioButton>(aspect))
                                      : AspectWidgets::createSubWidget<QCheckBox>(aspect);
        AspectWidgets::addButtonToLayout(aspect, parent, button);
    }

    static void renderFontFamily(FontFamilyAspect *aspect, Layout &parent,
                                 const AspectPresentation &pres)
    {
        if (QLabel *l = AspectWidgets::createLabel(aspect))
            parent.addItem(l);

        auto fontComboBox = AspectWidgets::createSubWidget<QFontComboBox>(aspect);
        fontComboBox->setFontFilters(QFontComboBox::FontFilters(pres.fontFilters.toInt()));
        // The QFontInfo hoop resolves to a family actually on the system.
        fontComboBox->setCurrentFont(QFontInfo(QFont(aspect->value())).family());
        parent.addItem(fontComboBox);

        QObject::connect(fontComboBox, &QFontComboBox::currentTextChanged, aspect,
                         [aspect](const QString &text) {
                             aspect->setVolatileVariantValueFromGui(text);
                         });
        // Like the inline body: an unconditional extra volatileValueChanged per
        // combo change, and no aspect-to-widget sync.
        QObject::connect(fontComboBox, &QFontComboBox::currentTextChanged,
                         aspect, &BaseAspect::volatileValueChanged);
    }

    static void renderSelection(SelectionAspect *aspect, Layout &parent,
                                const AspectPresentation &pres)
    {
        if (pres.control == AspectControls::RadioButtonGroup) {
            auto buttonGroup = new QButtonGroup(parent.product());
            buttonGroup->setObjectName(aspect->objectName());
            buttonGroup->setExclusive(true);
            for (int i = 0, n = int(pres.choices.size()); i < n; ++i) {
                const AspectPresentation::Choice &choice = pres.choices.at(i);
                auto button = AspectWidgets::createSubWidget<QRadioButton>(aspect, choice.display);
                button->setChecked(i == aspect->value());
                button->setEnabled(choice.enabled);
                button->setToolTip(choice.toolTip);
                parent.addItem(button);
                buttonGroup->addButton(button, i);
            }
            aspect->addOnVolatileValueChanged(buttonGroup, [aspect, buttonGroup] {
                QAbstractButton *button = buttonGroup->button(aspect->volatileValue());
                QTC_ASSERT(button, return);
                button->setChecked(true);
            });
            QObject::connect(buttonGroup, &QButtonGroup::idToggled, aspect,
                             [aspect, buttonGroup] {
                                 aspect->setVolatileVariantValueFromGui(
                                     buttonGroup->id(buttonGroup->checkedButton()));
                             });
            return;
        }

        if (!aspect->labelText().isEmpty()) {
            aspect->setLabelText(aspect->labelText());
        } else if (!aspect->displayName().isEmpty()) { // fallback for compatibility (< 20.0), but warn
            qWarning() << "Aspect" << aspect->displayName()
                       << "uses ComboBox display but does not set labelText()";
            aspect->setLabelText(aspect->displayName());
        }
        auto comboBox = AspectWidgets::createSubWidget<QComboBox>(aspect);
        comboBox->setObjectName(aspect->objectName());
        for (const AspectPresentation::Choice &choice : pres.choices)
            comboBox->addItem(choice.display);
        comboBox->setCurrentIndex(aspect->volatileValue());
        AspectWidgets::addLabeledItem(aspect, parent, comboBox);
        aspect->addOnVolatileValueChanged(comboBox, [comboBox, aspect] {
            comboBox->setCurrentIndex(aspect->volatileValue());
        });
        QObject::connect(comboBox, &QComboBox::currentIndexChanged, aspect,
                         [aspect, comboBox] {
                             aspect->setVolatileVariantValueFromGui(comboBox->currentIndex());
                         });
    }

    static void renderMultiSelection(MultiSelectionAspect *aspect, Layout &parent,
                                     const AspectPresentation &pres)
    {
        if (pres.choices.isEmpty())
            return;

        auto listView = AspectWidgets::createSubWidget<QListWidget>(aspect);
        for (const AspectPresentation::Choice &choice : pres.choices)
            (void) new QListWidgetItem(choice.display, listView);
        AspectWidgets::addLabeledItem(aspect, parent, listView);

        const int expectedCount = int(pres.choices.size());
        QObject::connect(listView, &QListWidget::itemChanged, aspect,
                         [aspect, listView, expectedCount] {
                             QStringList val;
                             const int n = listView->count();
                             QTC_CHECK(n == expectedCount);
                             for (int i = 0; i != n; ++i) {
                                 QListWidgetItem *item = listView->item(i);
                                 if (item->checkState() == Qt::Checked)
                                     val.append(item->text());
                             }
                             aspect->setVolatileVariantValueFromGui(val);
                         });

        const auto syncToGui = [aspect, listView, expectedCount] {
            const QSignalBlocker blocker(listView);
            const QStringList checked = aspect->volatileValue();
            const int n = listView->count();
            QTC_CHECK(n == expectedCount);
            for (int i = 0; i != n; ++i) {
                QListWidgetItem *item = listView->item(i);
                item->setCheckState(checked.contains(item->text()) ? Qt::Checked
                                                                   : Qt::Unchecked);
            }
        };
        aspect->addOnVolatileValueChanged(listView, syncToGui);
        syncToGui();
    }

    static void renderSpinBox(IntegerAspect *aspect, Layout &parent,
                              const AspectPresentation &pres)
    {
        auto spinBox = AspectWidgets::createSubWidget<QSpinBox>(aspect);
        spinBox->setDisplayIntegerBase(pres.displayIntegerBase);
        spinBox->setPrefix(pres.prefix);
        spinBox->setSuffix(pres.suffix);
        spinBox->setSingleStep(pres.singleStep.toInt());
        spinBox->setSpecialValueText(pres.specialValueText);
        const qint64 factor = pres.displayScaleFactor;
        if (pres.minimum.isValid() && pres.maximum.isValid())
            spinBox->setRange(int(pres.minimum.toLongLong() / factor),
                              int(pres.maximum.toLongLong() / factor));
        AspectWidgets::addLabeledItem(aspect, parent, spinBox);

        QObject::connect(spinBox, &QSpinBox::valueChanged, aspect, [aspect, spinBox, factor] {
            aspect->setVolatileVariantValueFromGui(qint64(spinBox->value()) * factor);
        });
        aspect->addOnVolatileValueChanged(spinBox, [aspect, spinBox, factor] {
            spinBox->setValue(int(aspect->volatileValue() / factor));
        });
        spinBox->setValue(int(aspect->volatileValue() / factor));
    }

    static void renderDoubleSpinBox(DoubleAspect *aspect, Layout &parent,
                                    const AspectPresentation &pres)
    {
        auto spinBox = AspectWidgets::createSubWidget<QDoubleSpinBox>(aspect);
        spinBox->setPrefix(pres.prefix);
        spinBox->setSuffix(pres.suffix);
        spinBox->setSingleStep(pres.singleStep.toDouble());
        spinBox->setSpecialValueText(pres.specialValueText);
        if (pres.minimum.isValid() && pres.maximum.isValid())
            spinBox->setRange(pres.minimum.toDouble(), pres.maximum.toDouble());
        AspectWidgets::addLabeledItem(aspect, parent, spinBox);

        QObject::connect(spinBox, &QDoubleSpinBox::valueChanged, aspect, [aspect, spinBox] {
            aspect->setVolatileVariantValueFromGui(spinBox->value());
        });
        aspect->addOnVolatileValueChanged(spinBox, [aspect, spinBox] {
            spinBox->setValue(aspect->volatileValue());
        });
        spinBox->setValue(aspect->volatileValue()); // Must happen after setRange()!
    }

    static void renderCommaSeparatedLineEdit(StringListAspect *aspect, Layout &parent)
    {
        auto lineEdit = AspectWidgets::createSubWidget<FancyLineEdit>(aspect);

        const auto listToText = [](const QStringList &list) { return list.join(","); };
        const auto textToList = [](const QString &text) {
            QStringList parts = text.split(',', Qt::SkipEmptyParts);
            for (QString &p : parts)
                p = p.trimmed();
            parts.removeAll({});
            return parts;
        };

        lineEdit->setText(listToText(aspect->volatileValue()));
        lineEdit->setReadOnly(aspect->isReadOnly());

        QObject::connect(lineEdit, &QLineEdit::textEdited, aspect,
                         [aspect, lineEdit, textToList] {
                             aspect->setVolatileVariantValueFromGui(textToList(lineEdit->text()));
                         });
        aspect->addOnVolatileValueChanged(lineEdit, [aspect, lineEdit, listToText, textToList] {
            if (textToList(lineEdit->text()) != aspect->volatileValue())
                lineEdit->setText(listToText(aspect->volatileValue()));
        });

        AspectWidgets::addLabeledItem(aspect, parent, lineEdit);
    }

    static void renderFilePathList(FilePathListAspect *aspect, Layout &parent,
                                   const AspectPresentation &pres)
    {
        PathListEditor *editor = AspectWidgets::createSubWidget<PathListEditor>(aspect);
        editor->setPathList(aspect->value());
        QObject::connect(editor, &PathListEditor::changed, aspect, [aspect, editor] {
            aspect->setVolatileVariantValueFromGui(editor->pathList());
        });
        aspect->addOnVolatileValueChanged(editor, [aspect, editor] {
            if (editor->pathList() != aspect->volatileValue())
                editor->setPathList(aspect->volatileValue());
        });
        // Like the inline body: the editor's change signal is forwarded
        // unconditionally.
        QObject::connect(editor, &PathListEditor::changed,
                         aspect, &BaseAspect::volatileValueChanged);

        editor->setToolTip(pres.toolTip);
        editor->setMaximumHeight(100);
        editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        editor->setPlaceholderText(pres.placeholderText);

        AspectWidgets::registerSubWidget(aspect, editor);

        QObject::connect(aspect, &FilePathListAspect::placeHolderTextChanged,
                         editor, &PathListEditor::setPlaceholderText);

        parent.addItem(editor);
    }

    static QString displayedString(StringAspect *aspect)
    {
        const std::function<QString(const QString &)> filter = aspect->displayFilter();
        return filter ? filter(aspect->volatileValue()) : aspect->volatileValue();
    }

    static void renderStringLabel(StringAspect *aspect, Layout &parent,
                                  const AspectPresentation &pres)
    {
        const QString displayed = displayedString(aspect);
        auto label = AspectWidgets::createSubWidget<ElidingLabel>(aspect);
        label->setElideMode(aspect->elideMode());
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setText(displayed);
        label->setToolTip(aspect->showToolTipOnLabel() ? displayed : pres.toolTip);
        QObject::connect(aspect, &StringAspect::elideModeChanged,
                         label, &ElidingLabel::setElideMode);
        AspectWidgets::addLabeledItem(aspect, parent, label);

        QObject::connect(&aspect->undoableValue().m_signal, &UndoSignaller::changed, label,
                         [aspect, label] {
                             label->setText(aspect->undoableValue().get());
                             label->setToolTip(aspect->showToolTipOnLabel()
                                                   ? aspect->undoableValue().get()
                                                   : aspect->toolTip());
                         });
    }

    static void renderStringLineEdit(StringAspect *aspect, Layout &parent,
                                     const AspectPresentation &pres)
    {
        auto lineEdit = AspectWidgets::createSubWidget<FancyLineEdit>(aspect);
        // Named after the setting, like a path chooser, so a page with several
        // of them can be told apart.
        lineEdit->setObjectName(Utils::stringFromKey(aspect->settingsKey()));
        AspectWidgets::addMacroExpansion(aspect, lineEdit);
        lineEdit->setPlaceholderText(pres.placeholderText);
        lineEdit->setMinimumHeight(aspect->minimumHeight());

        if (const QStringList completions = aspect->completions(); !completions.isEmpty())
            lineEdit->setSpecialCompleter(new QCompleter(completions, lineEdit));

        if (!aspect->rightSideIconPath().isEmpty()) {
            QIcon icon(aspect->rightSideIconPath().toFSPathString());
            QTC_CHECK(!icon.isNull());
            lineEdit->setButtonIcon(FancyLineEdit::Right, icon);
            lineEdit->setButtonVisible(FancyLineEdit::Right, true);
            QObject::connect(lineEdit, &FancyLineEdit::rightButtonClicked,
                             aspect, &StringAspect::rightSideIconClicked);
        }

        if (!aspect->historyCompleterKey().isEmpty())
            lineEdit->setHistoryCompleter(aspect->historyCompleterKey());

        QObject::connect(aspect, &StringAspect::historyCompleterKeyChanged, lineEdit,
                         [lineEdit](const Key &historyCompleterKey) {
                             lineEdit->setHistoryCompleter(historyCompleterKey);
                         });
        QObject::connect(aspect, &StringAspect::placeholderTextChanged,
                         lineEdit, &FancyLineEdit::setPlaceholderText);

        if (const std::optional<ValidationFunction> validator = aspect->validationFunction())
            lineEdit->setValidationFunction(*validator);
        else if (const auto validatorFactory = aspect->validatorFactory())
            lineEdit->setValidator(validatorFactory(lineEdit));

        lineEdit->setTextKeepingActiveCursor(displayedString(aspect));
        lineEdit->setReadOnly(aspect->isReadOnly());
        lineEdit->setValidatePlaceHolder(aspect->validatePlaceHolder());

        addCheckableLabeledItem(aspect, parent, lineEdit);

        if (pres.withResetButton) {
            auto resetButton = AspectWidgets::createSubWidget<QPushButton>(aspect, Tr::tr("Reset"));
            resetButton->setEnabled(lineEdit->text() != aspect->defaultValue());
            QObject::connect(resetButton, &QPushButton::clicked, lineEdit, [aspect, lineEdit] {
                lineEdit->setText(aspect->defaultValue());
            });
            QObject::connect(lineEdit, &QLineEdit::textChanged, resetButton,
                             [aspect, lineEdit, resetButton] {
                                 resetButton->setEnabled(lineEdit->text()
                                                         != aspect->defaultValue());
                             });
            parent.addItem(resetButton);
        }
        QObject::connect(lineEdit, &FancyLineEdit::validChanged,
                         aspect, &StringAspect::validChanged);
        aspect->volatileValueToGui();
        if (aspect->isAutoApply() && aspect->autoApplyOnEditingFinished()) {
            QObject::connect(lineEdit, &FancyLineEdit::editingFinished, aspect,
                             [aspect, lineEdit] {
                                 if (lineEdit->text() != aspect->undoableValue().get())
                                     aspect->setVolatileVariantValueFromGui(lineEdit->text());
                             });
        } else {
            QObject::connect(lineEdit, &QLineEdit::textChanged, aspect, [aspect, lineEdit] {
                aspect->setVolatileVariantValueFromGui(lineEdit->text());
            });
        }
        if (pres.control == AspectControls::PasswordLineEdit) {
            auto showPasswordButton = AspectWidgets::createSubWidget<ShowPasswordButton>(aspect);
            lineEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
            parent.addItem(showPasswordButton);
            QObject::connect(showPasswordButton, &ShowPasswordButton::toggled, lineEdit,
                             [showPasswordButton, lineEdit] {
                                 lineEdit->setEchoMode(showPasswordButton->isChecked()
                                                           ? QLineEdit::Normal
                                                           : QLineEdit::PasswordEchoOnEdit);
                             });
        }

        QObject::connect(&aspect->undoableValue().m_signal, &UndoSignaller::changed, lineEdit,
                         [aspect, lineEdit] {
                             if (lineEdit->text() != aspect->undoableValue().get())
                                 lineEdit->setTextKeepingActiveCursor(
                                     aspect->undoableValue().get());
                             lineEdit->validate();
                         });
    }

    static void renderStringTextEdit(StringAspect *aspect, Layout &parent,
                                     const AspectPresentation &pres)
    {
        auto textEdit = AspectWidgets::createSubWidget<QTextEdit>(aspect);
        AspectWidgets::addMacroExpansion(aspect, textEdit);
        textEdit->setPlaceholderText(pres.placeholderText);
        textEdit->setUndoRedoEnabled(false);
        textEdit->setAcceptRichText(aspect->acceptRichText());
        textEdit->setTextInteractionFlags(Qt::TextEditorInteraction);
        textEdit->setText(displayedString(aspect));
        textEdit->setReadOnly(aspect->isReadOnly());
        addCheckableLabeledItem(aspect, parent, textEdit);

        aspect->volatileValueToGui();
        QObject::connect(aspect, &StringAspect::acceptRichTextChanged,
                         textEdit, &QTextEdit::setAcceptRichText);
        QObject::connect(aspect, &StringAspect::placeholderTextChanged,
                         textEdit, &QTextEdit::setPlaceholderText);

        QObject::connect(textEdit, &QTextEdit::textChanged, aspect, [aspect, textEdit] {
            if (textEdit->toPlainText() != aspect->undoableValue().get())
                aspect->setVolatileVariantValueFromGui(textEdit->toPlainText());
        });

        QObject::connect(&aspect->undoableValue().m_signal, &UndoSignaller::changed, textEdit,
                         [aspect, textEdit] {
                             if (textEdit->toPlainText() != aspect->undoableValue().get())
                                 textEdit->setText(aspect->undoableValue().get());
                         });
    }

    static void renderPathChooser(FilePathAspect *aspect, Layout &parent,
                                  const AspectPresentation &pres)
    {
        const std::function<QString(const QString &)> filter = aspect->displayFilter();
        const QString displayed = filter ? filter(aspect->value()) : aspect->value();

        PathChooser *pathChooser = AspectWidgets::createSubWidget<PathChooser>(aspect);
        // A settings page tends to hold several of these, so name them apart.
        pathChooser->setObjectName(Utils::stringFromKey(aspect->settingsKey()));
        AspectWidgets::addMacroExpansion(aspect, pathChooser);
        applyPathChooserConfiguration(aspect, pathChooser);
        if (aspect->defaultValue() == aspect->value())
            pathChooser->setDefaultValue(FilePath::fromUserInput(aspect->defaultValue()));
        else
            pathChooser->setFilePath(FilePath::fromUserInput(displayed));
        // Do not override the default value with the placeholder, but use the
        // placeholder if the default is empty.
        if (pathChooser->lineEdit()->placeholderText().isEmpty())
            pathChooser->lineEdit()->setPlaceholderText(pres.placeholderText);
        addCheckableLabeledItem(aspect, parent, pathChooser);

        // The aspect's setters do not know this widget; they say what changed
        // and we re-read them. Everything but the value, which the user may be
        // in the middle of typing.
        QObject::connect(aspect, &BaseAspect::controlConfigurationChanged, pathChooser,
                         [aspect, pathChooser] {
                             applyPathChooserConfiguration(aspect, pathChooser);
                         });
        QObject::connect(aspect, &BaseAspect::controlFocusRequested,
                         pathChooser, qOverload<>(&QWidget::setFocus));
        QObject::connect(aspect, &BaseAspect::controlValidationRequested,
                         pathChooser, &PathChooser::triggerChanged);
        QObject::connect(pathChooser, &PathChooser::validChanged,
                         aspect, &FilePathAspect::setValid);

        QObject::connect(&aspect->undoableValue().m_signal, &UndoSignaller::changed, pathChooser,
                         [aspect, pathChooser] {
                             if (pathChooser->lineEdit()->text() != aspect->undoableValue().get())
                                 pathChooser->lineEdit()->setTextKeepingActiveCursor(
                                     aspect->undoableValue().get());
                         });

        aspect->volatileValueToGui();
        if (aspect->isAutoApply() && aspect->autoApplyOnEditingFinished()) {
            QObject::connect(pathChooser, &PathChooser::editingFinished, aspect,
                             [aspect, pathChooser] {
                                 if (aspect->editFinishedGuard().isLocked())
                                     return;
                                 const GuardLocker lk(aspect->editFinishedGuard());
                                 aspect->setVolatileVariantValueFromGui(
                                     pathChooser->lineEdit()->text());
                             });
            QObject::connect(pathChooser, &PathChooser::browsingFinished, aspect,
                             [aspect, pathChooser] {
                                 aspect->setVolatileVariantValueFromGui(
                                     pathChooser->lineEdit()->text());
                             });
        } else {
            QObject::connect(pathChooser, &PathChooser::textChanged, aspect,
                             [aspect](const QString &text) {
                                 aspect->setVolatileVariantValueFromGui(text);
                             });
        }
    }

    static void renderColor(ColorAspect *aspect, Layout &parent, const AspectPresentation &pres)
    {
        auto button = AspectWidgets::createSubWidget<QtColorButton>(aspect);
        button->setColor(aspect->volatileValue());
        button->setAlphaAllowed(pres.alphaAllowed);
        button->setMinimumSize(pres.minimumSize);

        QObject::connect(button, &QtColorButton::colorChanged, aspect, [aspect](const QColor &c) {
            aspect->setVolatileVariantValueFromGui(QVariant::fromValue(c));
        });

        aspect->addOnVolatileValueChanged(button, [aspect, button] {
            if (button->color() != aspect->volatileValue())
                button->setColor(aspect->volatileValue());
        });

        if (pres.withResetButton) {
            auto resetButton = AspectWidgets::createSubWidget<QPushButton>(aspect, Tr::tr("Reset"));
            resetButton->setToolTip(Tr::tr("Reset to default.", "Color"));
            QObject::connect(resetButton, &QAbstractButton::clicked, aspect, [aspect] {
                aspect->setVolatileValue(aspect->defaultValue());
            });
            AspectWidgets::addLabeledItems(aspect, parent, {button, resetButton});
        } else {
            QMenu *menu = new QMenu(button);
            QAction *resetAction = menu->addAction(Tr::tr("Reset to Default"), aspect, [aspect] {
                aspect->setVolatileValue(aspect->defaultValue());
            });
            resetAction->setIcon(button->generatePixmap());
            resetAction->setIconVisibleInMenu(true);
            button->setMenu(menu);
            button->setToolTip(QStringList{pres.toolTip,
                                           Tr::tr("Press and hold to reset to default.")}
                                   .join('\n'));
            AspectWidgets::addLabeledItem(aspect, parent, button);
        }
    }

    static InfoLabelType infoLabelType(AspectControls::InfoType infoType)
    {
        switch (infoType) {
        case AspectControls::InfoType::Information:
            return InfoLabelType::Information;
        case AspectControls::InfoType::Warning:
            return InfoLabelType::Warning;
        case AspectControls::InfoType::Error:
            return InfoLabelType::Error;
        case AspectControls::InfoType::Ok:
            return InfoLabelType::Ok;
        case AspectControls::InfoType::NotOk:
            return InfoLabelType::NotOk;
        case AspectControls::InfoType::None:
            break;
        }
        return InfoLabelType::None;
    }

    static void renderTextDisplay(TextDisplay *aspect, Layout &parent,
                                  const AspectPresentation &pres)
    {
        auto label = AspectWidgets::createSubWidget<InfoLabel>(aspect, aspect->text(),
                                                        infoLabelType(pres.infoType));
        label->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);
        label->setToolTip(pres.toolTip);
        QObject::connect(label, &QLabel::linkActivated, aspect, &TextDisplay::linkActivated);
        label->setElideMode(Qt::ElideNone);
        label->setWordWrap(pres.wordWrap);
        // Do not use label->setVisible(isVisible()) unconditionally, it
        // does not have a QWidget parent yet when used in a LayoutBuilder.
        if (!pres.visible)
            label->setVisible(false);

        QObject::connect(aspect, &TextDisplay::changed, label, [aspect, label] {
            label->setText(aspect->text());
        });
        QObject::connect(aspect, &BaseAspect::controlConfigurationChanged, label,
                         [aspect, label] {
                             const AspectPresentation p = aspect->presentation();
                             label->setType(infoLabelType(p.infoType));
                             label->setWordWrap(p.wordWrap);
                         });
        parent.addItem(label);
    }

    static void renderStringListTree(StringListAspect *aspect, Layout &parent,
                                     const AspectPresentation &pres)
    {
        auto editor = AspectWidgets::createSubWidget<QTreeWidget>(aspect);
        editor->setHeaderHidden(true);
        editor->setRootIsDecorated(false);
        editor->setEditTriggers(pres.allowEditing ? QAbstractItemView::AllEditTriggers
                                                  : QAbstractItemView::NoEditTriggers);

        QPushButton *add = pres.allowAdding
                               ? AspectWidgets::createSubWidget<QPushButton>(aspect, Tr::tr("Add"))
                               : nullptr;
        QPushButton *remove = pres.allowRemoving
                                  ? AspectWidgets::createSubWidget<QPushButton>(aspect, Tr::tr("Remove"))
                                  : nullptr;

        const auto itemsToStringList = [editor] {
            QStringList items;
            const QTreeWidgetItem *rootItem = editor->invisibleRootItem();
            for (int i = 0, count = rootItem->childCount(); i < count; ++i)
                items.append(rootItem->child(i)->data(0, Qt::DisplayRole).toString());
            return items;
        };

        const auto populate = [editor, aspect] {
            editor->clear();
            for (const QString &entry : aspect->undoableValue().get()) {
                auto item = new QTreeWidgetItem(editor, {entry});
                item->setData(0, Qt::ToolTipRole, entry);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            }
        };

        if (add) {
            QObject::connect(add, &QPushButton::clicked, aspect, [aspect, populate, editor] {
                // The row to edit is appended silently; the edit itself
                // records the undo command.
                aspect->undoableValue().setSilently(aspect->undoableValue().get() << QString());
                populate();
                const QTreeWidgetItem *root = editor->invisibleRootItem();
                QTreeWidgetItem *lastChild = root->child(root->childCount() - 1);
                const QModelIndex index = editor->indexFromItem(lastChild, 0);
                editor->edit(index);
            });
        }

        if (remove) {
            QObject::connect(remove, &QPushButton::clicked, aspect,
                             [aspect, editor, itemsToStringList] {
                                 const QList<QTreeWidgetItem *> selected = editor->selectedItems();
                                 QTC_ASSERT(selected.size() == 1, return);
                                 editor->invisibleRootItem()->removeChild(selected.first());
                                 delete selected.first();
                                 aspect->setVolatileVariantValueFromGui(itemsToStringList());
                             });
        }

        QObject::connect(&aspect->undoableValue().m_signal, &UndoSignaller::changed, editor,
                         [aspect, populate, itemsToStringList] {
                             if (itemsToStringList() != aspect->undoableValue().get())
                                 populate();
                         });

        QObject::connect(editor->model(), &QAbstractItemModel::dataChanged, aspect,
                         [aspect, itemsToStringList](const QModelIndex &tl, const QModelIndex &br,
                                                     const QList<int> &roles) {
                             if (!roles.contains(Qt::DisplayRole))
                                 return;
                             if (tl != br)
                                 return;
                             aspect->setVolatileVariantValueFromGui(itemsToStringList());
                         });

        populate();

        // clang-format off
        QWidget *mainWdgt = Widget {
            Row {
                noMargin,
                editor,
                If (pres.allowAdding || pres.allowRemoving) >> Then {
                    Column {
                        If (pres.allowAdding) >> Then {add},
                        If (pres.allowRemoving) >> Then {remove},
                        st,
                    }
                },
            }
        }.emerge();
        // clang-format on

        AspectWidgets::registerSubWidget(aspect, mainWdgt);

        parent.addItem(AspectWidgets::createLabel(aspect));
        parent.addItem(mainWdgt);
    }

    static void renderStringSelection(StringSelectionAspect *aspect, Layout &parent)
    {
        QTC_ASSERT(aspect->m_fillCallback, return);

        QComboBox *comboBox = AspectWidgets::createSubWidget<QComboBox>(aspect);

        QObject::connect(aspect, &StringSelectionAspect::modelChange, comboBox,
                         [aspect, comboBox, lastValue = QVariant()](bool changing) mutable {
                             if (changing) {
                                 comboBox->blockSignals(true);
                                 lastValue = aspect->volatileValue();
                             } else {
                                 comboBox->blockSignals(false);
                                 if (lastValue != QVariant(aspect->volatileValue())) {
                                     emit comboBox->currentIndexChanged(comboBox->currentIndex());
                                     emit comboBox->currentTextChanged(comboBox->currentText());
                                 }
                             }
                         });

        aspect->ensureFilled();

        comboBox->setInsertPolicy(QComboBox::InsertPolicy::NoInsert);
        comboBox->setEditable(aspect->m_comboBoxEditable);
        if (aspect->m_comboBoxEditable) {
            comboBox->completer()->setCompletionMode(QCompleter::PopupCompletion);
            comboBox->completer()->setFilterMode(Qt::MatchContains);
        }
        applyComboBoxSizing(comboBox, aspect->presentation());
        QObject::connect(aspect, &BaseAspect::controlConfigurationChanged, comboBox,
                         [aspect, comboBox] {
                             applyComboBoxSizing(comboBox, aspect->presentation());
                         });
        comboBox->setCurrentText(aspect->value());
        comboBox->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

        comboBox->setModel(aspect->m_model);
        Utils::setWheelScrollingWithoutFocusBlocked(comboBox);


        QObject::connect(aspect->m_selectionModel, &QItemSelectionModel::currentChanged, comboBox,
                         [comboBox](const QModelIndex &currentIdx) {
                             if (currentIdx.isValid()
                                 && comboBox->currentIndex() != currentIdx.row())
                                 comboBox->setCurrentIndex(currentIdx.row());
                         });

        QObject::connect(comboBox, &QComboBox::activated, aspect, [aspect](int idx) {
            const QModelIndex modelIdx = aspect->m_model->index(idx, 0);
            if (!modelIdx.isValid())
                return;

            const QString newValue = modelIdx.data(Qt::UserRole + 1).toString();
            aspect->m_undoable.set(aspect->undoStack(), newValue);
            aspect->volatileValueToGui();
        });

        QObject::connect(&aspect->m_undoable.m_signal, &UndoSignaller::changed, comboBox,
                         [aspect, comboBox] {
                             if (QStandardItem *item = aspect->itemById(aspect->m_undoable.get()))
                                 aspect->m_selectionModel->setCurrentIndex(
                                     item->index(), QItemSelectionModel::ClearAndSelect);
                             else
                                 comboBox->setCurrentText(aspect->m_undoable.get());

                             aspect->handleGuiChanged();
                         });

        if (aspect->m_selectionModel->currentIndex().isValid())
            comboBox->setCurrentIndex(aspect->m_selectionModel->currentIndex().row());

        AspectWidgets::addLabeledItem(aspect, parent, comboBox);
    }

    static void renderFontPicker(FontAspect *aspect, Layout &parent)
    {
        parent.addItem(aspect->fontFamily);

        QComboBox *sizeComboBox = AspectWidgets::createSubWidget<QComboBox>(aspect);
        parent.addItem(aspect->fontPointSize.labelText());
        parent.addItem(sizeComboBox);

        auto updateFontSizeSelector = [aspect, sizeComboBox] {
            const QString family = aspect->fontFamily.volatileValue();
            const QString fontStyle = QFontDatabase::styleString(aspect->volatileValue());

            QList<int> pointSizes = QFontDatabase::pointSizes(family, fontStyle);
            if (pointSizes.empty())
                pointSizes = QFontDatabase::standardSizes();

            const QSignalBlocker blocker(sizeComboBox);
            sizeComboBox->clear();
            sizeComboBox->setCurrentIndex(-1);
            sizeComboBox->setEnabled(!pointSizes.empty());

            if (pointSizes.empty())
                return;

            QString n;
            for (const int pointSize : std::as_const(pointSizes))
                sizeComboBox->addItem(n.setNum(pointSize), QVariant(pointSize));

            const int desiredPointSize = aspect->fontPointSize.volatileValue();

            // Keep the selection, or take the closest available size.
            int closestIndex = -1;
            int closestAbsError = 0xFFFF;

            const int pointSizeCount = sizeComboBox->count();
            for (int i = 0; i < pointSizeCount; i++) {
                const int itemPointSize = sizeComboBox->itemData(i).toInt();
                const int absError = qAbs(desiredPointSize - itemPointSize);
                if (absError < closestAbsError) {
                    closestIndex = i;
                    closestAbsError = absError;
                    if (closestAbsError == 0)
                        break;
                } else { // Past the optimum.
                    if (absError > closestAbsError)
                        break;
                }
            }

            if (closestIndex != -1)
                sizeComboBox->setCurrentIndex(closestIndex);
        };

        updateFontSizeSelector();

        QObject::connect(sizeComboBox, &QComboBox::currentIndexChanged, aspect,
                         [aspect, sizeComboBox] {
                             int fontSize = 14;
                             const int currentIndex = sizeComboBox->currentIndex();
                             if (currentIndex != -1)
                                 fontSize = sizeComboBox->itemData(currentIndex).toInt();
                             aspect->fontPointSize.setVolatileValue(fontSize);
                         });

        aspect->fontFamily.addOnVolatileValueChanged(sizeComboBox, updateFontSizeSelector);
    }
};

bool renderAspectToLayout(BaseAspect &aspect, Layouting::Layout &parent)
{
    return AspectWidgetRenderer::render(aspect, parent);
}

} // namespace Utils::Internal

namespace Utils {

void installAspectWidgetRenderer()
{
    static bool installed = false;
    QTC_CHECK(!installed);
    installed = true;
    setAspectRenderer(&Internal::renderAspectToLayout);
}

} // namespace Utils

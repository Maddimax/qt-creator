// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectwidgetrenderer.h"

#include "aspects.h"
#include "fancylineedit.h"
#include "layoutbuilder.h"
#include "pathlisteditor.h"
#include "qtcassert.h"
#include "stylehelper.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFontComboBox>
#include <QFontInfo>
#include <QLabel>
#include <QListWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace Layouting;

namespace Utils::Internal {

// Rebuilds the widgets of the generic addToLayoutImpl() bodies in aspects.cpp,
// driven by presentation() instead of the aspects' private state. GUI writes go
// through setVolatileVariantValueFromGui(), which records undo like the bodies'
// direct UndoableValue writes; GUI reads follow volatileValueChanged().
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
        // Not handled:
        // - ColorPicker: presentation() lacks the color button's minimum size.
        // - Label: TextDisplay owns its live label; setIconType() and
        //   setWordWrap() mutate it directly, with no signal to follow.
        // - StringList: the tree editor's add flow needs
        //   UndoableValue::setSilently on the aspect's private undoable.
        default:
            return false;
        }
    }

private:
    static void renderBool(BoolAspect *aspect, Layout &parent, const AspectPresentation &pres)
    {
        QAbstractButton *button = pres.control == AspectControls::RadioButton
                                      ? static_cast<QAbstractButton *>(
                                            aspect->createSubWidget<QRadioButton>())
                                      : aspect->createSubWidget<QCheckBox>();

        switch (pres.labelPlacement) {
        case AspectControls::LabelPlacement::Compact:
            button->setText(pres.labelText);
            parent.addItem(button);
            break;
        case AspectControls::LabelPlacement::AtControl:
            button->setText(pres.labelText);
            parent.addItem(empty);
            parent.addItem(button);
            break;
        case AspectControls::LabelPlacement::InExtraLabel:
            aspect->addLabeledItem(parent, button);
            break;
        case AspectControls::LabelPlacement::ShowTip: {
            parent.addItem(empty);
            button->setText(pres.labelText);
            auto ttLabel = new QLabel(pres.toolTip);
            ttLabel->setFont(StyleHelper::uiFont(StyleHelper::UiElementLabelSmall));
            auto lt = new QVBoxLayout;
            lt->setContentsMargins({});
            lt->setSpacing(StyleHelper::SpacingTokens::GapVXs);
            lt->addWidget(button);
            lt->addWidget(ttLabel);
            parent.addItem(lt);
            break;
        }
        }

        QObject::connect(button, &QAbstractButton::clicked, aspect, [button, aspect] {
            aspect->setVolatileVariantValueFromGui(button->isChecked());
        });
        aspect->addOnVolatileValueChanged(button, [button, aspect] {
            button->setChecked(aspect->volatileValue());
        });
        button->setChecked(aspect->volatileValue());
    }

    static void renderFontFamily(FontFamilyAspect *aspect, Layout &parent,
                                 const AspectPresentation &pres)
    {
        if (QLabel *l = aspect->createLabel())
            parent.addItem(l);

        auto fontComboBox = aspect->createSubWidget<QFontComboBox>();
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
                auto button = aspect->createSubWidget<QRadioButton>(choice.display);
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
        auto comboBox = aspect->createSubWidget<QComboBox>();
        comboBox->setObjectName(aspect->objectName());
        for (const AspectPresentation::Choice &choice : pres.choices)
            comboBox->addItem(choice.display);
        comboBox->setCurrentIndex(aspect->volatileValue());
        aspect->addLabeledItem(parent, comboBox);
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

        auto listView = aspect->createSubWidget<QListWidget>();
        for (const AspectPresentation::Choice &choice : pres.choices)
            (void) new QListWidgetItem(choice.display, listView);
        aspect->addLabeledItem(parent, listView);

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
        auto spinBox = aspect->createSubWidget<QSpinBox>();
        spinBox->setDisplayIntegerBase(pres.displayIntegerBase);
        spinBox->setPrefix(pres.prefix);
        spinBox->setSuffix(pres.suffix);
        spinBox->setSingleStep(pres.singleStep.toInt());
        spinBox->setSpecialValueText(pres.specialValueText);
        const qint64 factor = pres.displayScaleFactor;
        if (pres.minimum.isValid() && pres.maximum.isValid())
            spinBox->setRange(int(pres.minimum.toLongLong() / factor),
                              int(pres.maximum.toLongLong() / factor));
        aspect->addLabeledItem(parent, spinBox);

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
        auto spinBox = aspect->createSubWidget<QDoubleSpinBox>();
        spinBox->setPrefix(pres.prefix);
        spinBox->setSuffix(pres.suffix);
        spinBox->setSingleStep(pres.singleStep.toDouble());
        spinBox->setSpecialValueText(pres.specialValueText);
        if (pres.minimum.isValid() && pres.maximum.isValid())
            spinBox->setRange(pres.minimum.toDouble(), pres.maximum.toDouble());
        aspect->addLabeledItem(parent, spinBox);

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
        auto lineEdit = aspect->createSubWidget<FancyLineEdit>();

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

        aspect->addLabeledItem(parent, lineEdit);
    }

    static void renderFilePathList(FilePathListAspect *aspect, Layout &parent,
                                   const AspectPresentation &pres)
    {
        PathListEditor *editor = aspect->createSubWidget<PathListEditor>();
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

        aspect->registerSubWidget(editor);

        QObject::connect(aspect, &FilePathListAspect::placeHolderTextChanged,
                         editor, &PathListEditor::setPlaceholderText);

        parent.addItem(editor);
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

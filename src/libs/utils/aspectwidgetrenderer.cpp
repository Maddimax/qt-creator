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
#include "groupedlistaspect.h"
#include "groupedmodel.h"
#include "groupedview.h"
#include "guiutils.h"
#include "hostosinfo.h"
#include "utilsicons.h"
#include "infolabel.h"
#include "layoutbuilder.h"
#include "passworddialog.h"
#include "pathchooser.h"
#include "pathlisteditor.h"
#include "qtcassert.h"
#include "qtcolorbutton.h"
#include "stylehelper.h"
#include "utilstr.h"
#include "widgets.h"

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
#include <QAction>
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
            // Whatever asked for a check box gets one. What it holds is read
            // and written as a variant, so the aspect need not be a BoolAspect
            // - a run configuration's TerminalAspect is not.
            renderBool(&aspect, parent, pres);
            return true;
        case AspectControls::TriStateCheckBox:
            if (auto triStateAspect = qobject_cast<TriStateAspect *>(&aspect)) {
                renderTriState(triStateAspect, parent, pres);
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
            // A StringSelectionAspect fills its entries from a callback and
            // refills them while the page is open, so it keeps its own path.
            if (auto stringSelectionAspect = qobject_cast<StringSelectionAspect *>(&aspect)) {
                renderStringSelection(stringSelectionAspect, parent);
                return true;
            }
            // Everything else that asked for a combo gets one from the choices
            // in its descriptor, whatever type it is.
            renderSelection(&aspect, parent, pres);
            return true;
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
            // Whatever asked for a list of paths gets one. What it holds is
            // read and written as a variant, so it need not be a
            // FilePathListAspect - Valgrind's suppression files are not.
            renderFilePathList(&aspect, parent, pres);
            return true;
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
        case AspectControls::Button:
            renderButton(&aspect, parent, pres);
            return true;
        case AspectControls::TextWithAction:
            renderTextWithAction(&aspect, parent, pres);
            return true;
        case AspectControls::Secret:
            renderSecret(&aspect, parent, pres);
            return true;
        case AspectControls::GroupedList:
            if (auto groupedList = qobject_cast<GroupedListAspect *>(&aspect)) {
                renderGroupedList(groupedList, parent);
                return true;
            }
            return false;
        case AspectControls::Container:
            if (auto container = qobject_cast<AspectContainer *>(&aspect)) {
                // A container that reads as one value has no layout of its own
                // to give: what it holds goes in one row, labelled once.
                // hasLayouter(), not layouter(): the latter falls back to a
                // column of the aspects and so is never empty, which made
                // this branch unreachable and setInlineRow() a Quick-only
                // setting for as long as it has existed.
                if (pres.inlineRow && !AspectWidgets::hasLayouter(container)) {
                    // The controls go in one row, and the row goes where any
                    // other control would: in the field column, with the label
                    // beside it. Keeping the label inside the row instead made
                    // the controls start after the label's own width rather
                    // than at the field column, so an ABI row did not line up
                    // with the rows above it. InlineGroupDelegate has always
                    // put the label in a column of its own.
                    Layouting::Row row{Layouting::noMargin};
                    for (BaseAspect * const child : container->aspects())
                        row.addItem(child);
                    row.addItem(Layouting::st);
                    AspectWidgets::addLabeledItem(&aspect, parent, row.emerge());
                    return true;
                }
                // No box of its own: what it holds belongs to the layout
                // around it, so that its rows line up with everything else's
                // rather than in a widget of their own.
                if (pres.flattened && !AspectWidgets::hasLayouter(container)) {
                    for (BaseAspect * const child : container->aspects()) {
                        parent.addItem(child);
                        parent.flush();
                    }
                    return true;
                }
                if (const AspectWidgets::Layouter l = AspectWidgets::layouter(container)) {
                    // In a widget of its own, so that a container can be shown
                    // and hidden as a unit - one category of a code style at a
                    // time. Added as a bare layout there is nothing to hide,
                    // and setVisible() on the container did nothing.
                    // With a group box around it where it has a title, which
                    // GroupDelegate has always drawn: a named group of settings
                    // that runs into the ones above it is not a group.
                    QWidget *widget = pres.labelText.isEmpty()
                        ? Layouting::Column{l(), Layouting::noMargin}.emerge()
                        : Layouting::Group{Layouting::title(pres.labelText), l()}.emerge();
                    widget->setVisible(container->isVisible());
                    QObject::connect(container, &BaseAspect::visibleChanged,
                                     widget, &QWidget::setVisible);
                    parent.addItem(widget);
                }
                return true;
            }
            return false;
        // Not handled:
        default:
            return false;
        }
    }

private:
    // The tree and the buttons are members of the GroupedView rather than heap
    // allocations, so whatever holds them has to outlive the layout they are
    // put in - a widget deleting them as children would be freeing what it
    // never allocated. Holding the view by value gets that for nothing:
    // members go before ~QWidget deletes children, and each one detaches
    // itself on the way.
    class GroupedListWidget : public QWidget
    {
    public:
        explicit GroupedListWidget(GroupedModel &model)
            : m_view(model)
        {
            using namespace Layouting;
            Row {
                &m_view.view(),
                Column {
                    &m_view.cloneButton(),
                    &m_view.removeButton(),
                    &m_view.makeDefaultButton(),
                    st,
                },
                noMargin,
            }.attachTo(this);
        }

        GroupedView &view() { return m_view; }

    private:
        GroupedView m_view;
    };

    // Items in named groups, drawn with the QTreeView the pages already used.
    // A view of its own rather than one built from the descriptor, so it goes
    // in whole; the aspect's own selection stays the one in charge.
    static void renderGroupedList(GroupedListAspect *aspect, Layout &parent)
    {
        QTC_ASSERT(aspect->model(), return);
        auto widget = new GroupedListWidget(*aspect->model());
        GroupedView &view = widget->view();
        QObject::connect(&view, &GroupedView::currentRowChanged, aspect,
                         [aspect](int, int newRow) { aspect->setCurrentRow(newRow); });
        QObject::connect(aspect, &GroupedListAspect::currentRowChanged, &view,
                         [&view](int, int newRow) {
                             if (view.currentRow() != newRow)
                                 view.selectRow(newRow);
                         });
        parent.addItem(widget);
    }

    // A value the aspect does not keep and has to go and get. It arrives after
    // the field exists, so the aspect says when the field may be typed in:
    // read-only until then, because typing before the secret is there would
    // store nothing over what is already in the keychain. Failing to read it
    // leaves the field read-only with the reason as its placeholder.
    static void renderSecret(BaseAspect *aspect, Layout &parent,
                             const AspectPresentation &pres)
    {
        auto lineEdit = AspectWidgets::createSubWidget<FancyLineEdit>(aspect);
        lineEdit->setObjectName(pres.objectName);
        lineEdit->setEchoMode(QLineEdit::Password);
        lineEdit->setPlaceholderText(pres.placeholderText);
        lineEdit->setReadOnly(pres.readOnly);

        auto reveal = AspectWidgets::createSubWidget<ShowPasswordButton>(aspect);
        reveal->setEnabled(!pres.readOnly);
        QObject::connect(reveal, &ShowPasswordButton::toggled, lineEdit, [reveal, lineEdit] {
            lineEdit->setEchoMode(reveal->isChecked() ? QLineEdit::Normal : QLineEdit::Password);
        });

        QLabel *warning = nullptr;
        if (pres.infoType != AspectControls::InfoType::None) {
            warning = AspectWidgets::createSubWidget<QLabel>(aspect);
            warning->setPixmap(Icons::WARNING.icon().pixmap(16, 16));
            warning->setToolTip(pres.toolTip);
        }

        QObject::connect(aspect, &BaseAspect::readOnlyChanged, lineEdit,
                         [aspect, lineEdit, reveal] {
                             lineEdit->setReadOnly(aspect->isReadOnly());
                             reveal->setEnabled(!aspect->isReadOnly());
                         });
        QObject::connect(aspect, &BaseAspect::placeholderTextChanged,
                         lineEdit, &QLineEdit::setPlaceholderText);
        QObject::connect(aspect, &BaseAspect::displayTextChanged, lineEdit, [aspect, lineEdit] {
            if (lineEdit->text() != aspect->displayText())
                lineEdit->setTextKeepingActiveCursor(aspect->displayText());
        });
        QObject::connect(lineEdit, &QLineEdit::textChanged, aspect, [aspect](const QString &text) {
            aspect->setVolatileVariantValue(text);
        });

        AspectWidgets::addLabeledItem(aspect, parent,
                                      Row{noMargin, lineEdit, warning, reveal}.emerge());
        aspect->requestDisplayText();
    }

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

    static void renderButton(BaseAspect *aspect, Layout &parent, const AspectPresentation &pres)
    {
        // An OptionPushButton where the button both acts and offers: it keeps
        // its own click and shows the menu only from the arrow.
        QPushButton *button = pres.actionIsDefault
            ? AspectWidgets::createSubWidget<OptionPushButton>(aspect)
            : AspectWidgets::createSubWidget<QPushButton>(aspect);
        button->setText(pres.actionText);
        button->setIcon(pres.actionIcon);
        button->setToolTip(pres.toolTip);
        button->setEnabled(pres.enabled);
        button->setVisible(pres.visible);
        if (pres.choices.isEmpty() || pres.actionIsDefault) {
            QObject::connect(button, &QAbstractButton::clicked, aspect, [aspect] {
                aspect->triggerAction();
            });
        }
        if (!pres.choices.isEmpty()) {
            // A button that offers rather than does: one entry per choice, each
            // handed back by id.
            auto menu = new QMenu(button);
            for (const AspectPresentation::Choice &choice : pres.choices) {
                QAction * const action = menu->addAction(choice.display);
                action->setToolTip(choice.toolTip);
                action->setEnabled(choice.enabled);
                QObject::connect(action, &QAction::triggered, aspect, [aspect, id = choice.id] {
                    static_cast<ActionAspect *>(aspect)->triggerChoice(id);
                });
            }
            // A button that also acts keeps its click and puts the menu
            // behind the arrow; one that only offers is the menu.
            if (pres.actionIsDefault)
                static_cast<OptionPushButton *>(button)->setOptionalMenu(menu);
            else
                button->setMenu(menu);
            if (HostOsInfo::isMacHost())
                button->setStyleSheet("text-align:center;");
        }
        parent.addItem(button);
        // Same as the Quick delegate: the aspect is being drawn, so let it find
        // out what its label should say.
        aspect->requestDisplayText();
    }

    // A value that is edited elsewhere: a summary of it, and the one button
    // that opens whatever edits it.
    static void renderTextWithAction(BaseAspect *aspect, Layout &parent,
                                     const AspectPresentation &pres)
    {
        auto summary = AspectWidgets::createSubWidget<ElidingLabel>(aspect);
        summary->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        summary->setElideMode(Qt::ElideRight);
        summary->setText(aspect->displayText());
        QObject::connect(aspect, &BaseAspect::displayTextChanged, summary, [aspect, summary] {
            summary->setText(aspect->displayText());
        });
        addContextAction(aspect, summary, pres);

        auto button = AspectWidgets::createSubWidget<QPushButton>(aspect, pres.actionText);
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        QObject::connect(button, &QAbstractButton::clicked, aspect, [aspect] {
            aspect->triggerAction();
        });

        // createLabel() may return nullptr; addEmpty == false drops it then.
        parent.addItems({AspectWidgets::createLabel(aspect), summary, button},
                        /*addEmpty=*/false);
        // As the Quick delegate: the aspect is being drawn, so let it find out
        // what it should say.
        aspect->requestDisplayText();
    }

    static void renderBool(BaseAspect *aspect, Layout &parent, const AspectPresentation &pres)
    {
        QAbstractButton *button = pres.control == AspectControls::RadioButton
                                      ? static_cast<QAbstractButton *>(
                                            AspectWidgets::createSubWidget<QRadioButton>(aspect))
                                      : AspectWidgets::createSubWidget<QCheckBox>(aspect);
        AspectWidgets::addButtonToLayout(aspect, parent, button);
    }

    // On, off, or neither, as one check box. Qt's own tri-state cycling goes
    // through all three; "neither" is what the setting says when nothing has
    // been decided, so it is shown but not cycled to.
    static void renderTriState(TriStateAspect *aspect, Layout &parent,
                               const AspectPresentation &pres)
    {
        auto button = AspectWidgets::createSubWidget<QCheckBox>(aspect);
        button->setText(pres.labelText);
        button->setTristate(true);

        const auto toGui = [aspect, button] {
            const TriState state = TriState::fromInt(aspect->volatileValue());
            button->setCheckState(state == TriState::Enabled     ? Qt::Checked
                                  : state == TriState::Disabled  ? Qt::Unchecked
                                                                 : Qt::PartiallyChecked);
        };
        toGui();
        QObject::connect(aspect, &BaseAspect::volatileValueChanged, button, toGui);
        QObject::connect(button, &QCheckBox::clicked, aspect, [aspect, button] {
            const TriState state = button->checkState() == Qt::Checked ? TriState::Enabled
                                                                      : TriState::Disabled;
            aspect->setVolatileVariantValueFromGui(state.toVariant());
        });
        parent.addItem(button);
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

    // A checkable entry on the control's context menu, for a state that is
    // about the setting rather than about its value.
    static void addContextAction(BaseAspect *aspect, QWidget *control,
                                 const AspectPresentation &pres)
    {
        if (pres.contextActionText.isEmpty())
            return;
        auto action = new QAction(pres.contextActionText, control);
        action->setCheckable(true);
        action->setChecked(pres.contextActionChecked);
        action->setEnabled(pres.contextActionEnabled);
        QObject::connect(action, &QAction::toggled, aspect, [aspect](bool checked) {
            aspect->triggerContextAction(checked);
        });
        control->addAction(action);
        control->setContextMenuPolicy(Qt::ActionsContextMenu);
    }

    // Which entry a value stands for. An aspect whose value is the index says
    // so by leaving valueIsChoiceId false; one whose value is the choice's own
    // id - a launcher, a device - says true, and is looked up.
    static int indexForValue(const AspectPresentation &pres, const QVariant &value)
    {
        if (!pres.valueIsChoiceId)
            return value.toInt();
        for (int i = 0, n = int(pres.choices.size()); i < n; ++i) {
            if (pres.choices.at(i).id == value)
                return i;
        }
        return -1;
    }

    static QVariant valueForIndex(const AspectPresentation &pres, int index)
    {
        if (!pres.valueIsChoiceId)
            return index;
        if (index < 0 || index >= int(pres.choices.size()))
            return {};
        return pres.choices.at(index).id;
    }

    static void renderSelection(BaseAspect *aspect, Layout &parent,
                                const AspectPresentation &pres)
    {
        if (pres.control == AspectControls::RadioButtonGroup) {
            auto buttonGroup = new QButtonGroup(parent.product());
            buttonGroup->setObjectName(aspect->objectName());
            buttonGroup->setExclusive(true);
            for (int i = 0, n = int(pres.choices.size()); i < n; ++i) {
                const AspectPresentation::Choice &choice = pres.choices.at(i);
                auto button = AspectWidgets::createSubWidget<QRadioButton>(aspect, choice.display);
                button->setChecked(i == indexForValue(pres, aspect->volatileVariantValue()));
                button->setEnabled(choice.enabled);
                button->setToolTip(choice.toolTip);
                parent.addItem(button);
                buttonGroup->addButton(button, i);
            }
            aspect->addOnVolatileValueChanged(buttonGroup, [aspect, buttonGroup, pres] {
                QAbstractButton *button
                    = buttonGroup->button(indexForValue(pres, aspect->volatileVariantValue()));
                QTC_ASSERT(button, return);
                button->setChecked(true);
            });
            QObject::connect(buttonGroup, &QButtonGroup::idToggled, aspect,
                             [aspect, buttonGroup, pres] {
                                 aspect->setVolatileVariantValueFromGui(valueForIndex(
                                     pres, buttonGroup->id(buttonGroup->checkedButton())));
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

        // The descriptor is read again each time rather than captured: an
        // aspect refills its entries while the page is open - the launchers a
        // device offers, the ABIs a toolchain has - and says so with
        // controlConfigurationChanged(). A captured copy would leave the
        // control showing a list that is gone, and, worse, would write back
        // the id at that position in the old list.
        const auto refill = [aspect, comboBox] {
            const AspectPresentation now = aspect->presentation();
            // clear() and addItem() move the current index, which would be
            // written back as the user's doing.
            const QSignalBlocker blocker(comboBox);
            comboBox->clear();
            for (const AspectPresentation::Choice &choice : now.choices) {
                comboBox->addItem(choice.icon, choice.display);
                comboBox->setItemData(comboBox->count() - 1, choice.toolTip, Qt::ToolTipRole);
            }
            applyComboBoxSizing(comboBox, now);
            comboBox->setCurrentIndex(indexForValue(now, aspect->volatileVariantValue()));
        };
        refill();
        QObject::connect(aspect, &BaseAspect::controlConfigurationChanged, comboBox, refill);

        addContextAction(aspect, comboBox, pres);
        AspectWidgets::addLabeledItem(aspect, parent, comboBox);
        aspect->addOnVolatileValueChanged(comboBox, [comboBox, aspect] {
            comboBox->setCurrentIndex(
                indexForValue(aspect->presentation(), aspect->volatileVariantValue()));
        });
        QObject::connect(comboBox, &QComboBox::currentIndexChanged, aspect,
                         [aspect, comboBox] {
                             aspect->setVolatileVariantValueFromGui(
                                 valueForIndex(aspect->presentation(), comboBox->currentIndex()));
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

    static void renderFilePathList(BaseAspect *aspect, Layout &parent,
                                   const AspectPresentation &pres)
    {
        PathListEditor *editor = AspectWidgets::createSubWidget<PathListEditor>(aspect);
        editor->setPathList(aspect->volatileVariantValue().toStringList());
        QObject::connect(editor, &PathListEditor::changed, aspect, [aspect, editor] {
            aspect->setVolatileVariantValueFromGui(editor->pathList());
        });
        aspect->addOnVolatileValueChanged(editor, [aspect, editor] {
            const QStringList paths = aspect->volatileVariantValue().toStringList();
            if (editor->pathList() != paths)
                editor->setPathList(paths);
        });
        // Like the inline body: the editor's change signal is forwarded
        // unconditionally.
        QObject::connect(editor, &PathListEditor::changed,
                         aspect, &BaseAspect::volatileValueChanged);

        editor->setToolTip(pres.toolTip);
        editor->setMaximumHeight(100);
        editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        editor->setPlaceholderText(pres.placeholderText);
        // A list of directories browses for one; a list of files says which
        // files, and Insert... asks for those instead.
        editor->setFileDialogTitle(pres.promptDialogTitle);
        if (pres.pathKind == AspectControls::PathKind::File
            || pres.pathKind == AspectControls::PathKind::SaveFile) {
            editor->setFileDialogFilter(pres.promptDialogFilter);
        }

        AspectWidgets::registerSubWidget(aspect, editor);

        QObject::connect(aspect, &BaseAspect::placeholderTextChanged,
                         editor, &PathListEditor::setPlaceholderText);

        AspectWidgets::addLabeledItem(aspect, parent, editor);
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
        QObject::connect(aspect, &BaseAspect::controlFocusRequested, lineEdit, [lineEdit] {
            lineEdit->selectAll();
            lineEdit->setFocus();
        });

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

    static Qt::TextFormat textFormat(AspectControls::TextFormat format)
    {
        switch (format) {
        case AspectControls::TextFormat::PlainText:    return Qt::PlainText;
        case AspectControls::TextFormat::RichText:     return Qt::RichText;
        case AspectControls::TextFormat::MarkdownText: return Qt::MarkdownText;
        case AspectControls::TextFormat::AutoText:     break;
        }
        return Qt::AutoText;
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
        label->setTextFormat(textFormat(pres.textFormat));
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
                             label->setTextFormat(textFormat(p.textFormat));
                         });
        // With its own label where it has one, which the Quick delegate has
        // always done: a read-only "Version:" row is a name and a value, and
        // this drew only the value.
        AspectWidgets::addLabeledItem(aspect, parent, label);
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

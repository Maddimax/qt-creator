// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectwidgets.h"

#include "dirtysettings.h"
#include "guiutils.h"
#include "layoutbuilder.h"
#include "macroexpander.h"
#include "pathchooser.h"
#include "qtcassert.h"
#include "stylehelper.h"
#include "variablechooser.h"

#include <QAbstractButton>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QPointer>

namespace Utils::AspectWidgets {

void registerSubWidget(BaseAspect *aspect, QWidget *widget)
{
    widget->setEnabled(aspect->isEnabled());
    widget->setToolTip(aspect->toolTip());

    // An aspect that is not what a page is dirty about - a button that opens a
    // dialog - says so on itself, not on a control it does not build.
    if (isIgnoredForDirtyHook(aspect))
        setIgnoreForDirtyHook(widget);

    // Visible is on by default. Not setting it explicitly avoid popping
    // it up when the parent is not set yet, the normal case.
    if (!aspect->isVisible())
        widget->setVisible(false);

    QObject::connect(aspect, &BaseAspect::enabledChanged, widget, [aspect, widget] {
        widget->setEnabled(aspect->isEnabled());
    });
    QObject::connect(aspect, &BaseAspect::visibleChanged, widget, &QWidget::setVisible);
    QObject::connect(aspect, &BaseAspect::tooltipChanged, widget, &QWidget::setToolTip);

    if (auto lineEdit = qobject_cast<QLineEdit *>(widget))
        QObject::connect(aspect, &BaseAspect::readOnlyChanged, lineEdit, &QLineEdit::setReadOnly);
    else if (auto textEdit = qobject_cast<QTextEdit *>(widget))
        QObject::connect(aspect, &BaseAspect::readOnlyChanged, textEdit, &QTextEdit::setReadOnly);
    else if (auto pathChooser = qobject_cast<PathChooser *>(widget))
        QObject::connect(aspect, &BaseAspect::readOnlyChanged, pathChooser, &PathChooser::setReadOnly);

    QObject::connect(aspect, &BaseAspect::destroyed, widget, &QObject::deleteLater);
}

void improveWheelScrolling(QWidget *widget)
{
    setWheelScrollingWithoutFocusBlocked(widget);
}

QLabel *createLabel(BaseAspect *aspect)
{
    const QPixmap labelPixmap = aspect->presentation().labelPixmap;
    if (aspect->labelText().isEmpty() && labelPixmap.isNull())
        return nullptr;

    auto label = new QLabel(aspect->labelText());
    label->setTextInteractionFlags(label->textInteractionFlags() | Qt::TextSelectableByMouse);
    QObject::connect(label, &QLabel::linkActivated, aspect, [aspect](const QString &link) {
        emit aspect->labelLinkActivated(link);
    });
    if (!labelPixmap.isNull())
        label->setPixmap(labelPixmap);
    registerSubWidget(aspect, label);

    QObject::connect(aspect, &BaseAspect::labelTextChanged, label, [label, aspect] {
        label->setText(aspect->labelText());
    });
    QObject::connect(aspect, &BaseAspect::labelPixmapChanged, label, [label, aspect] {
        label->setPixmap(aspect->presentation().labelPixmap);
    });

    return label;
}

QLabel *addLabeledItem(BaseAspect *aspect, Layouting::Layout &parent, QWidget *widget)
{
    const AspectPresentation pres = aspect->presentation();
    if (!pres.objectName.isEmpty())
        widget->setObjectName(pres.objectName);
    if (QLabel *l = createLabel(aspect)) {
        l->setBuddy(widget);
        parent.addItem(l);
        parent.addItem(Layouting::Span(std::max(pres.spanX - 1, 1), widget));
        return l;
    }
    // Without a label of its own the control starts in the label's column, so
    // the whole span is its own. A warning under a setting says setSpan(2) and
    // means both columns; before this it meant nothing at all here.
    if (pres.spanX > 1) {
        parent.addItem(Layouting::Span(pres.spanX, widget));
        return {};
    }
    parent.addItem(widget);
    return {};
}

void addLabeledItems(BaseAspect *aspect, Layouting::Layout &parent, const QList<QWidget *> &widgets)
{
    if (QLabel *l = createLabel(aspect))
        parent.addItem(l);
    for (QWidget *widget : widgets)
        parent.addItem(widget);
}

void addMacroExpansion(BaseAspect *aspect, QWidget *widget)
{
    const auto varChooser = new VariableChooser(widget);
    varChooser->addMacroExpanderProvider({aspect, [aspect] { return aspect->macroExpander(); }});
    if (auto pathChooser = qobject_cast<PathChooser *>(widget)) {
        pathChooser->setMacroExpander(aspect->macroExpander());
        varChooser->addSupportedWidget(pathChooser->lineEdit());
    } else {
        varChooser->addSupportedWidget(widget);
    }
}

QWidget *createConfigWidget(BaseAspect *aspect)
{
    const BaseAspect::ConfigWidgetCreator creator = aspect->configWidgetCreator();
    QWidget *configWidget = creator ? creator() : nullptr;
    if (configWidget)
        registerSubWidget(aspect, configWidget);

    return configWidget;
}

void addButtonToLayout(BaseAspect *aspect, Layouting::Layout &parent,
                       QAbstractButton *button)
{
    const AspectPresentation pres = aspect->presentation();
    switch (pres.labelPlacement) {
    case AspectControls::LabelPlacement::Compact:
        button->setText(pres.labelText);
        parent.addItem(button);
        break;
    case AspectControls::LabelPlacement::AtControl:
        button->setText(pres.labelText);
        parent.addItem(Layouting::empty);
        parent.addItem(button);
        break;
    case AspectControls::LabelPlacement::InExtraLabel:
        addLabeledItem(aspect, parent, button);
        break;
    case AspectControls::LabelPlacement::BesideControl: {
        // The box draws no text; the text is a label of its own so that what
        // is in it - a link to the page these settings come from - is more
        // than something to read.
        auto label = new QLabel(pres.labelText);
        label->setTextInteractionFlags(label->textInteractionFlags()
                                       | Qt::LinksAccessibleByMouse
                                       | Qt::TextSelectableByMouse);
        label->setToolTip(pres.toolTip);
        QObject::connect(label, &QLabel::linkActivated, aspect, [aspect](const QString &link) {
            aspect->activateLink(link);
        });
        registerSubWidget(aspect, label);
        parent.addItem(Layouting::empty);
        parent.addItem(Layouting::Row{Layouting::noMargin, button, label, Layouting::st});
        break;
    }
    case AspectControls::LabelPlacement::ShowTip: {
        parent.addItem(Layouting::empty);
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
    // The variant, not BoolAspect::volatileValue(): a check box is what the
    // descriptor asked for, and an aspect that asks for one need not be a
    // BoolAspect. TerminalAspect is one that is not.
    aspect->addOnVolatileValueChanged(button, [button, aspect] {
        button->setChecked(aspect->volatileVariantValue().toBool());
    });
    button->setChecked(aspect->volatileVariantValue().toBool());
}

std::function<void(Layouting::Layout *)> adoptButton(BoolAspect *aspect, QAbstractButton *button)
{
    return [aspect, button](Layouting::Layout *layout) {
        addButtonToLayout(aspect, *layout, button);
    };
}

std::function<void(QObject *)> groupChecker(BoolAspect *aspect)
{
    return [aspect](QObject *target) {
        auto groupBox = qobject_cast<QGroupBox *>(target);
        QTC_ASSERT(groupBox, return);
        registerSubWidget(aspect, groupBox);
        groupBox->setCheckable(true);

        QObject::connect(groupBox, &QGroupBox::clicked, aspect, [groupBox, aspect] {
            aspect->setVolatileVariantValueFromGui(groupBox->isChecked());
        });
        aspect->addOnVolatileValueChanged(groupBox, [groupBox, aspect] {
            groupBox->setChecked(aspect->volatileValue());
        });
        groupBox->setChecked(aspect->volatileValue());
    };
}

std::function<void(QObject *)> visibleController(BaseAspect *aspect)
{
    return [aspect](QObject *target) {
        auto widget = qobject_cast<QWidget *>(target);
        QTC_ASSERT(widget, return);
        // Only hide here: showing a not-yet-parented widget makes it pop up
        // as a top-level window. Later changes arrive once it is parented.
        if (!aspect->isVisible())
            widget->setVisible(false);
        QObject::connect(aspect, &BaseAspect::visibleChanged, widget, [aspect, widget] {
            widget->setVisible(aspect->isVisible());
        });
    };
}

void setLayouter(AspectContainer *container, const Layouter &layouter)
{
    container->setBackendData(std::make_shared<Layouter>(layouter));
}

bool hasLayouter(const AspectContainer *container)
{
    return bool(std::static_pointer_cast<Layouter>(container->backendData()));
}

Layouter layouter(const AspectContainer *container)
{
    const std::shared_ptr<Layouter> stored
        = std::static_pointer_cast<Layouter>(container->backendData());
    if (stored)
        return *stored;

    // A container that has been given QML instead of a layouter can still end
    // up inside a widget layout - an AspectList's details pane calls this for
    // every item. Callers invoke the result, so never hand back an empty
    // function: lay the aspects out in order, which is all a Column of them
    // would have done.
    return [container] {
        Layouting::Column column;
        for (BaseAspect *aspect : container->aspects())
            column.addItem(aspect);
        return column;
    };
}

namespace {

// Building a page is not showing it: the page census builds every one of them
// and shows none. See AspectContainer::pageShown().
class ShowReportingWidget final : public QWidget
{
public:
    explicit ShowReportingWidget(AspectContainer *container)
        : m_container(container)
    {}

private:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        if (m_container)
            m_container->pageShown();
    }

    const QPointer<AspectContainer> m_container;
};

AspectFormFactory s_aspectFormFactory;
AspectFormFactory s_genericAspectFormFactory;

} // namespace

void setAspectFormFactory(const AspectFormFactory &factory)
{
    s_aspectFormFactory = factory;
}

QWidget *createAspectForm(AspectContainer *container)
{
    QTC_ASSERT(container, return nullptr);

    if (s_aspectFormFactory) {
        if (QWidget *form = s_aspectFormFactory(container))
            return form;
    }

    const Layouter layouter = AspectWidgets::layouter(container);
    QTC_ASSERT(layouter, return nullptr);
    auto form = new ShowReportingWidget(container);
    layouter().attachTo(form);
    return form;
}

void setGenericAspectFormFactory(const AspectFormFactory &factory)
{
    s_genericAspectFormFactory = factory;
}

QWidget *createGenericAspectForm(AspectContainer *container)
{
    QTC_ASSERT(container, return nullptr);
    return s_genericAspectFormFactory ? s_genericAspectFormFactory(container) : nullptr;
}

} // namespace Utils::AspectWidgets

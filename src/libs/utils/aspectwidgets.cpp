// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectwidgets.h"

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

namespace Utils::AspectWidgets {

void registerSubWidget(BaseAspect *aspect, QWidget *widget)
{
    widget->setEnabled(aspect->isEnabled());
    widget->setToolTip(aspect->toolTip());

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

void addButtonToLayout(BoolAspect *aspect, Layouting::Layout &parent, QAbstractButton *button)
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
    aspect->addOnVolatileValueChanged(button, [button, aspect] {
        button->setChecked(aspect->volatileValue());
    });
    button->setChecked(aspect->volatileValue());
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

} // namespace Utils::AspectWidgets

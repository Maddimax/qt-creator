// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspectwidgets.h"

#include "guiutils.h"
#include "layoutbuilder.h"
#include "macroexpander.h"
#include "pathchooser.h"
#include "variablechooser.h"

#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>

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

} // namespace Utils::AspectWidgets

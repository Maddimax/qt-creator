// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectconfiguration.h"

#include "projectexplorerconstants.h"
#include "target.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/qtcassert.h>

#include <QVBoxLayout>

using namespace Utils;

namespace ProjectExplorer {

ProjectConfiguration::ProjectConfiguration(Target *target, Id id)
    : m_target(target)
    , m_id(id)
{
    QTC_CHECK(target);
    QTC_CHECK(id.isValid());
    setObjectName(id.toString());
}

ProjectConfiguration::~ProjectConfiguration() = default;

Project *ProjectConfiguration::project() const
{
    return m_target->project();
}

Kit *ProjectConfiguration::kit() const
{
    return m_target->kit();
}

Id ProjectConfiguration::id() const
{
    return m_id;
}

void ProjectConfiguration::setDisplayName(const QString &name)
{
    if (m_displayName.setValue(name))
        emit displayNameChanged();
}

void ProjectConfiguration::setDefaultDisplayName(const QString &name)
{
    if (m_displayName.setDefaultValue(name))
        emit displayNameChanged();
}

void ProjectConfiguration::setToolTip(const QString &text)
{
    if (text == m_toolTip)
        return;
    m_toolTip = text;
    emit toolTipChanged();
}

QString ProjectConfiguration::toolTip() const
{
    return m_toolTip;
}

void ProjectConfiguration::toMap(Store &map) const
{
    QTC_CHECK(m_id.isValid());
    map.insert(Constants::CONFIGURATION_ID_KEY, m_id.toSetting());
    m_displayName.toMap(map, Constants::DISPLAY_NAME_KEY);
    AspectContainer::toMap(map);
}

Target *ProjectConfiguration::target() const
{
    return m_target;
}

void ProjectConfiguration::fromMap(const Store &map)
{
    Id id = Id::fromSetting(map.value(Constants::CONFIGURATION_ID_KEY));
    // Note: This is only "startsWith", not ==, as RunConfigurations currently still
    // mangle in their build keys.
    QTC_ASSERT(id.name().startsWith(m_id.name()), reportError(); return);

    m_displayName.fromMap(map, Constants::DISPLAY_NAME_KEY);
    AspectContainer::fromMap(map);
}

namespace {

// Building a form is not showing it. What a container works out only when it
// is looked at - a build step asking its toolchain whether it supports
// parallel jobs - hangs off pageShown(), and the Qt Quick form reports it. So
// must this one, or that work would happen on one path and not the other.
class ShowReportingWidget final : public QWidget
{
public:
    ShowReportingWidget(AspectContainer *container, QWidget *form)
        : m_container(container)
    {
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(form);
    }

private:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        if (m_container)
            m_container->pageShown();
    }

    const QPointer<AspectContainer> m_container;
};

} // namespace

QWidget *createAspectsForm(AspectContainer *container)
{
    QTC_ASSERT(container, return nullptr);

    // Its own QML first, where it names one: a build step that has been given
    // a layout of its own is not a list of rows. Only where it names one -
    // createAspectForm() falls back to the widget layouter rather than
    // returning nothing, so asking it unconditionally would draw every
    // container that has no QML of its own with widgets.
    if (!container->qmlSource().isEmpty()) {
        if (QWidget * const form = Core::createAspectForm(container))
            return form;
    }

    if (QWidget * const form = Core::createGenericAspectForm(container))
        return form;

    Layouting::Form rows;
    rows.setNoMargins();
    for (BaseAspect *aspect : std::as_const(*container)) {
        rows.addItem(aspect);
        rows.flush();
    }
    return new ShowReportingWidget(container, rows.emerge());
}

Id idFromMap(const Store &map)
{
    return Id::fromSetting(map.value(Constants::CONFIGURATION_ID_KEY));
}

QString ProjectConfiguration::expandedDisplayName() const
{
    return macroExpander()->expand(m_displayName.value());
}

} // namespace ProjectExplorer

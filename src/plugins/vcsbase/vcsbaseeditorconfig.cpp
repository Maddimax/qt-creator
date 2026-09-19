// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcsbaseeditorconfig.h"

#include "vcsbasetr.h"

#include <utils/aspects.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QStandardItemModel>
#include <QStringList>

using namespace Utils;

namespace VcsBase {
namespace Internal {

class SettingMappingData
{
public:
    enum Type
    {
        Invalid,
        AspectBool,
        AspectString,
        AspectInt
    };

    SettingMappingData() : boolAspectSetting(nullptr)
    { }

    SettingMappingData(BoolAspect *setting) : boolAspectSetting(setting), m_type(AspectBool)
    { }

    SettingMappingData(StringAspect *setting) : stringAspectSetting(setting), m_type(AspectString)
    { }

    SettingMappingData(IntegerAspect *setting) : intAspectSetting(setting), m_type(AspectInt)
    { }

    Type type() const
    {
        return m_type;
    }

    union {
        BoolAspect *boolAspectSetting;
        StringAspect *stringAspectSetting;
        IntegerAspect *intAspectSetting;
    };

private:
    Type m_type = Invalid;
};

class VcsBaseEditorConfigPrivate
{
public:
    QStringList m_baseArguments;
    QList<VcsBaseEditorConfig::OptionMapping> m_optionMappings;
    QHash<QObject *, SettingMappingData> m_settingMapping;
    QList<QAction *> m_actions;
    QList<VcsBaseEditorChoice *> m_choices;
    QList<TextEditor::ToolBarField *> m_fields;
};

} // namespace Internal

VcsBaseEditorChoice::VcsBaseEditorChoice(VcsBaseEditorConfig *config, const QString &title,
                                         const QList<QVariant> &values, const QStringList &texts)
    : ToolBarChoice(config)
    , m_config(config)
    , m_model(new QStandardItemModel(this))
    , m_title(title)
    , m_values(values)
{
    for (const QString &text : texts)
        m_model->appendRow(new QStandardItem(text));
}

QAbstractItemModel *VcsBaseEditorChoice::model() const
{
    return m_model;
}

int VcsBaseEditorChoice::currentIndex() const
{
    return m_current;
}

QString VcsBaseEditorChoice::toolTip() const
{
    return m_title;
}

bool VcsBaseEditorChoice::isAvailable() const
{
    return true;
}

// A pick here is not one the language would have made differently, so there
// is nothing to clear and no way back to offer.
bool VcsBaseEditorChoice::isChosen() const
{
    return false;
}

void VcsBaseEditorChoice::choose(int index)
{
    if (index == m_current || index < 0 || index >= m_values.size())
        return;
    setCurrentIndex(index);
    emit m_config->argumentsChanged();
}

void VcsBaseEditorChoice::clearChoice()
{
}

int VcsBaseEditorChoice::count() const
{
    return m_values.size();
}

QVariant VcsBaseEditorChoice::currentValue() const
{
    return m_values.value(m_current);
}

int VcsBaseEditorChoice::indexOfValue(const QVariant &value) const
{
    return m_values.indexOf(value);
}

void VcsBaseEditorChoice::setCurrentIndex(int index)
{
    if (index == m_current || index < 0 || index >= m_values.size())
        return;
    m_current = index;
    emit changed();
}

/*!
    \class VcsBase::VcsBaseEditorConfig

    \brief The VcsBaseEditorConfig is an action and choice aggregator for use
    with a VcsBase::VcsEditorDocument, influencing for example the generation
    of version control diff output.

    The class maintains a list of command line arguments (starting from baseArguments())
    which are set according to the state of its toggles and choices. A change signal is
    provided that should trigger the rerun of the version control operation.
*/

VcsBaseEditorConfig::ChoiceItem::ChoiceItem(const QString &text, const QVariant &val) :
    displayText(text),
    value(val)
{
}

VcsBaseEditorConfig::VcsBaseEditorConfig(QObject *parent) :
    QObject(parent), d(new Internal::VcsBaseEditorConfigPrivate)
{
    connect(this, &VcsBaseEditorConfig::argumentsChanged,
            this, &VcsBaseEditorConfig::handleArgumentsChanged);
}

VcsBaseEditorConfig::~VcsBaseEditorConfig()
{
    delete d;
}

QStringList VcsBaseEditorConfig::baseArguments() const
{
    return d->m_baseArguments;
}

void VcsBaseEditorConfig::setBaseArguments(const QStringList &b)
{
    d->m_baseArguments = b;
}

QAction *VcsBaseEditorConfig::addReloadButton()
{
    auto action = new QAction(Icons::RELOAD_TOOLBAR.icon(), Tr::tr("Reload"), this);
    connect(action, &QAction::triggered, this, &VcsBaseEditorConfig::argumentsChanged);
    addAction(action);
    return action;
}

QStringList VcsBaseEditorConfig::arguments() const
{
    // Compile effective arguments
    QStringList args = baseArguments();
    for (const OptionMapping &mapping : optionMappings())
        args += argumentsForOption(mapping);
    return args;
}

QAction *VcsBaseEditorConfig::addToggleButton(const QString &option,
                                              const QString &label,
                                              const QString &tooltip)
{
    return addToggleButton(option.isEmpty() ? QStringList() : QStringList(option), label, tooltip);
}

QAction *VcsBaseEditorConfig::addToggleButton(const QStringList &options,
                                              const QString &label,
                                              const QString &tooltip)
{
    auto action = new QAction(label, this);
    action->setToolTip(tooltip);
    action->setCheckable(true);
    connect(action, &QAction::toggled, this, &VcsBaseEditorConfig::argumentsChanged);
    addAction(action);
    d->m_optionMappings.append(OptionMapping(options, action));
    return action;
}

VcsBaseEditorChoice *VcsBaseEditorConfig::addChoices(const QString &title,
                                                     const QStringList &options,
                                                     const QList<ChoiceItem> &items)
{
    QList<QVariant> values;
    QStringList texts;
    for (const ChoiceItem &item : items) {
        values.append(item.value);
        texts.append(item.displayText);
    }
    auto choice = new VcsBaseEditorChoice(this, title, values, texts);
    d->m_choices.append(choice);
    d->m_optionMappings.append(OptionMapping(options, choice));
    return choice;
}

void VcsBaseEditorConfig::addAction(QAction *action)
{
    d->m_actions.append(action);
}

TextEditor::ToolBarField *VcsBaseEditorConfig::addTextField(const QString &placeholderText,
                                                            const QString &toolTip)
{
    auto field = new TextEditor::ToolBarField(placeholderText, toolTip, this);
    connect(field, &TextEditor::ToolBarField::committed,
            this, &VcsBaseEditorConfig::argumentsChanged);
    d->m_fields.append(field);
    return field;
}

void VcsBaseEditorConfig::mapSetting(QAction *button, BoolAspect *setting)
{
    if (!d->m_settingMapping.contains(button) && button) {
        d->m_settingMapping.insert(button, Internal::SettingMappingData(setting));
        if (setting) {
            QSignalBlocker blocker(button);
            button->setChecked(setting->value());
        }
    }
}

void VcsBaseEditorConfig::mapSetting(VcsBaseEditorChoice *choice, StringAspect *setting)
{
    if (!d->m_settingMapping.contains(choice) && choice) {
        d->m_settingMapping.insert(choice, Internal::SettingMappingData(setting));
        if (setting) {
            const int itemIndex = choice->indexOfValue(setting->value());
            if (itemIndex != -1)
                choice->setCurrentIndex(itemIndex);
        }
    }
}

void VcsBaseEditorConfig::mapSetting(VcsBaseEditorChoice *choice, IntegerAspect *setting)
{
    if (d->m_settingMapping.contains(choice) || !choice)
        return;

    d->m_settingMapping.insert(choice, Internal::SettingMappingData(setting));

    if (!setting || setting->value() < 0 || setting->value() >= choice->count())
        return;

    choice->setCurrentIndex(setting->value());
}

QList<QAction *> VcsBaseEditorConfig::actions() const
{
    return d->m_actions;
}

QList<VcsBaseEditorChoice *> VcsBaseEditorConfig::choices() const
{
    return d->m_choices;
}

QList<TextEditor::ToolBarField *> VcsBaseEditorConfig::fields() const
{
    return d->m_fields;
}

void VcsBaseEditorConfig::handleArgumentsChanged()
{
    updateMappedSettings();
    executeCommand();
}

void VcsBaseEditorConfig::executeCommand()
{
    emit commandExecutionRequested();
}

VcsBaseEditorConfig::OptionMapping::OptionMapping(const QStringList &optionList, QObject *obj) :
    options(optionList),
    object(obj)
{
}

const QList<VcsBaseEditorConfig::OptionMapping> &VcsBaseEditorConfig::optionMappings() const
{
    return d->m_optionMappings;
}

QStringList VcsBaseEditorConfig::argumentsForOption(const OptionMapping &mapping) const
{
    auto action = qobject_cast<const QAction *>(mapping.object);
    if (action && action->isChecked())
        return mapping.options;

    QStringList args;
    auto choice = qobject_cast<const VcsBaseEditorChoice *>(mapping.object);
    if (!choice)
        return args;

    const QString value = choice->currentValue().toString();
    if (value.isEmpty())
        return args;

    if (mapping.options.isEmpty())
        args += value.split(' ');
    else
        args += mapping.options.first().arg(value);
    return args;
}

void VcsBaseEditorConfig::updateMappedSettings()
{
    for (const OptionMapping &optMapping : optionMappings()) {
        if (d->m_settingMapping.contains(optMapping.object)) {
            Internal::SettingMappingData& settingData = d->m_settingMapping[optMapping.object];
            switch (settingData.type()) {
            case Internal::SettingMappingData::AspectBool :
            {
                if (auto action = qobject_cast<const QAction *>(optMapping.object))
                    settingData.boolAspectSetting->setValue(action->isChecked());
                break;
            }
            case Internal::SettingMappingData::AspectString :
            {
                auto choice = qobject_cast<const VcsBaseEditorChoice *>(optMapping.object);
                if (choice && choice->currentIndex() != -1)
                    settingData.stringAspectSetting->setValue(choice->currentValue().toString());
                break;
            }
            case Internal::SettingMappingData::AspectInt:
            {
                auto choice = qobject_cast<const VcsBaseEditorChoice *>(optMapping.object);
                if (choice && choice->currentIndex() != -1)
                    settingData.intAspectSetting->setValue(choice->currentIndex());
                break;
            }
            case Internal::SettingMappingData::Invalid : break;
            } // end switch ()
        }
    }
}

} // namespace VcsBase

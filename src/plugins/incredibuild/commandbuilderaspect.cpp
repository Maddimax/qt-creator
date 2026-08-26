// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "commandbuilderaspect.h"

#include "cmakecommandbuilder.h"
#include "incredibuildconstants.h"
#include "incredibuildtr.h"
#include "makecommandbuilder.h"

#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/project.h>

#include <utils/pathchooser.h>

using namespace ProjectExplorer;
using namespace Utils;

namespace IncrediBuild::Internal {

class CommandBuilderAspectPrivate
{
public:
    CommandBuilderAspectPrivate(BuildStep *step)
        : m_buildStep{step},
          m_customCommandBuilder{step},
          m_makeCommandBuilder{step},
          m_cmakeCommandBuilder{step}
     {}

    void tryToMigrate();
    void setActiveCommandBuilder(const QString &commandBuilderId);

    BuildStep *m_buildStep;
    CommandBuilder m_customCommandBuilder;
    MakeCommandBuilder m_makeCommandBuilder;
    CMakeCommandBuilder m_cmakeCommandBuilder;

    CommandBuilder *m_commandBuilders[3] {
        &m_customCommandBuilder,
        &m_makeCommandBuilder,
        &m_cmakeCommandBuilder
    };

    // Default to "Custom Command", but try to upgrade in tryToMigrate() later.
    CommandBuilder *m_activeCommandBuilder = m_commandBuilders[0];

    bool m_loadedFromMap = false;

    // Set while the aspects are being written from the helper, so that what
    // they emit on the way is not read back as the user's doing.
    bool showing = false;
};

CommandBuilderAspect::CommandBuilderAspect(BuildStep *step)
    : AspectContainer(step)
    , d(new CommandBuilderAspectPrivate(step))
{
    // Three rows of the step, not a group of their own.
    setFlattened(true);

    helper.setLabelText(Tr::tr("Command Helper:"));
    helper.setToolTip(Tr::tr("Select a helper to establish the build command."));
    helper.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    for (CommandBuilder * const p : d->m_commandBuilders)
        helper.addOption(p->displayName());

    command.setLabelText(Tr::tr("Make command:"));
    command.setExpectedKind(PathChooserKind::ExistingCommand);
    command.setBaseDirectory(PathChooser::homePath());
    command.setHistoryCompleter("IncrediBuild.BuildConsole.MakeCommand.History");

    arguments.setLabelText(Tr::tr("Make arguments:"));
    arguments.setDisplayStyle(StringAspect::LineEditDisplay);

    // Behaviour, not layout. Picking a helper changes what the other two hold
    // and what they would hold if left alone; typing in them is what the
    // helper is told.
    helper.addOnVolatileValueChanged(this, [this] {
        const int index = helper.volatileValue();
        if (index >= 0 && index < int(std::size(d->m_commandBuilders)))
            d->m_activeCommandBuilder = d->m_commandBuilders[index];
        showActiveHelper();
    });
    command.addOnVolatileValueChanged(this, [this] {
        if (!d->showing)
            d->m_activeCommandBuilder->setCommand(command.expandedVolatileValue());
    });
    arguments.addOnVolatileValueChanged(this, [this] {
        if (!d->showing)
            d->m_activeCommandBuilder->setArguments(arguments.volatileValue());
    });

    showActiveHelper();
}

void CommandBuilderAspect::showActiveHelper()
{
    d->showing = true;
    for (int i = 0, n = int(std::size(d->m_commandBuilders)); i < n; ++i) {
        if (d->m_commandBuilders[i] == d->m_activeCommandBuilder)
            helper.setVolatileValue(i);
    }
    command.setDefaultPathValue(d->m_activeCommandBuilder->defaultCommand());
    command.setVolatileValue(d->m_activeCommandBuilder->command().toUrlishString());
    arguments.setPlaceHolderText(d->m_activeCommandBuilder->defaultArguments());
    arguments.setVolatileValue(d->m_activeCommandBuilder->arguments());
    d->showing = false;
}

void CommandBuilderAspect::requestDisplayText()
{
    // On first creation of the step, attempt to detect and migrate from
    // preceding steps. A step that was restored has been told what it is.
    if (!d->m_loadedFromMap) {
        d->m_loadedFromMap = true;
        d->tryToMigrate();
        showActiveHelper();
    }
}

CommandBuilderAspect::~CommandBuilderAspect()
{
    delete d;
}

QString CommandBuilderAspect::fullCommandFlag(bool keepJobNum) const
{
    QString argsLine = d->m_activeCommandBuilder->arguments();

    if (!keepJobNum)
        argsLine = d->m_activeCommandBuilder->setMultiProcessArg(argsLine);

    QString fullCommand("\"%1\" %2");
    fullCommand = fullCommand.arg(d->m_activeCommandBuilder->effectiveCommand().toUserOutput(), argsLine);

    return fullCommand;
}

void CommandBuilderAspectPrivate::setActiveCommandBuilder(const QString &commandBuilderId)
{
    for (CommandBuilder *p : m_commandBuilders) {
        if (p->id() == commandBuilderId) {
            m_activeCommandBuilder = p;
            break;
        }
    }
}

void CommandBuilderAspectPrivate::tryToMigrate()
{
    // This function is called when creating a fresh build step.
    // Attempt to detect build system from pre-existing steps.
    for (CommandBuilder *p : m_commandBuilders) {
        const QList<Utils::Id> migratableSteps = p->migratableSteps();
        for (Utils::Id stepId : migratableSteps) {
            if (BuildStep *bs = m_buildStep->stepList()->firstStepWithId(stepId)) {
                m_activeCommandBuilder = p;
                bs->setStepEnabled(false);
                m_buildStep->project()->saveSettings();
                return;
            }
        }
    }
}

void CommandBuilderAspect::fromMap(const Store &map)
{
    d->m_loadedFromMap = true;

    d->setActiveCommandBuilder(map.value(settingsKey()).toString());
    d->m_customCommandBuilder.fromMap(map);
    d->m_makeCommandBuilder.fromMap(map);
    d->m_cmakeCommandBuilder.fromMap(map);

    showActiveHelper();
}

void CommandBuilderAspect::toMap(Store &map) const
{
    map[IncrediBuild::Constants::INCREDIBUILD_BUILDSTEP_TYPE]
            = QVariant(IncrediBuild::Constants::BUILDCONSOLE_BUILDSTEP_ID);
    map[settingsKey()] = QVariant(d->m_activeCommandBuilder->id());

    d->m_customCommandBuilder.toMap(&map);
    d->m_makeCommandBuilder.toMap(&map);
    d->m_cmakeCommandBuilder.toMap(&map);
}

} // IncrediBuild::Internal

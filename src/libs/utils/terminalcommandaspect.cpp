// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "terminalcommandaspect.h"

#include "commandline.h"
#include "guiutils.h"
#include "hostosinfo.h"
#include "qtcassert.h"
#include "layoutbuilder.h"
#include "pathchooser.h"
#include "terminalcommand.h"
#include "utilstr.h"

#include <QDialog>
#include <QDialogButtonBox>

namespace Utils {

static QString msgTerminalHereAction()
{
    if (HostOsInfo::isWindowsHost())
        return Tr::tr("Open Command Prompt Here");
    return Tr::tr("Open Terminal Here");
}

TerminalCommandAspect::TerminalCommandAspect(AspectContainer *parentContainer)
    : AspectContainer(parentContainer)
{
    terminalEmulator.setSettingsKey(kTerminalCommandKey);
    terminalEmulator.setToolTip(Tr::tr("The terminal emulator to use for opening a terminal."));
    terminalEmulator.setLabelText(Tr::tr("Command:"));
    terminalEmulator.setExpectedKind(PathChooserKind::ExistingCommand);
    terminalEmulator.setDefaultPathValue(TerminalCommand::defaultTerminalEmulator().command);

    terminalOpenArgs.setSettingsKey(kTerminalOpenOptionsKey);
    terminalOpenArgs.setToolTip(
        Tr::tr("Command line arguments used for \"%1\".").arg(msgTerminalHereAction()));
    terminalOpenArgs.setLabelText(Tr::tr("\"%1\" arguments:").arg(msgTerminalHereAction()));
    terminalOpenArgs.setDisplayStyle(StringAspect::LineEditDisplay);
    terminalOpenArgs.setDefaultValue(TerminalCommand::defaultTerminalEmulator().openArgs);

    terminalExecuteArgs.setSettingsKey(kTerminalExecuteOptionsKey);
    terminalExecuteArgs.setToolTip(Tr::tr("Command line arguments used for \"Run in terminal\"."));
    terminalExecuteArgs.setLabelText(Tr::tr("\"Run in Terminal\" arguments:"));
    terminalExecuteArgs.setDisplayStyle(StringAspect::LineEditDisplay);
    terminalExecuteArgs.setDefaultValue(TerminalCommand::defaultTerminalEmulator().executeArgs);

    // The fields are the dialog's; the page shows what they come to.
    command.setVisible(false);
    setInlineRow(true);

    customize.setActionText(Tr::tr("Customize..."));
    customize.setSummaryProvider([this] { return summary(); });
    customize.setAction([this] { openDialog(); });
    // The summary is worked out from three other aspects, so none of them
    // knows on its own that it has changed.
    const QList<BaseAspect *> fields{&terminalEmulator, &terminalOpenArgs, &terminalExecuteArgs};
    for (BaseAspect * const field : fields) {
        connect(field, &BaseAspect::volatileValueChanged,
                &customize, &ActionAspect::updateSummary);
    }

    presets.setActionText(Tr::tr("Presets"));
    QList<AspectPresentation::Choice> choices;
    const QList<TerminalCommand> available = TerminalCommand::availableTerminalEmulators();
    for (int i = 0, n = int(available.size()); i < n; ++i)
        choices.append({available.at(i).command.toUserOutput(), {}, true, i});
    presets.setChoices(choices);
    presets.setOnChoice([this, available](const QVariant &id) {
        const int index = id.toInt();
        QTC_ASSERT(index >= 0 && index < available.size(), return);
        const TerminalCommand &term = available.at(index);
        terminalEmulator.setVolatileValue(term.command.toUrlishString());
        terminalOpenArgs.setVolatileValue(term.openArgs);
        terminalExecuteArgs.setVolatileValue(term.executeArgs);
    });
}

QString TerminalCommandAspect::summary() const
{
    const FilePath exe = terminalEmulator.expandedVolatileValue();
    return QString("%1: %2, %3: %4")
        .arg(msgTerminalHereAction())
        .arg(CommandLine(exe, terminalOpenArgs.volatileValue(), CommandLine::Raw).toUserOutput())
        .arg(Tr::tr("Run in Terminal"))
        .arg(CommandLine(exe, terminalExecuteArgs.volatileValue(), CommandLine::Raw).toUserOutput());
}

void TerminalCommandAspect::openDialog()
{
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok);

    // clang-format off
    auto layout = Layouting::Column {
        Layouting::Form {
            terminalEmulator, Layouting::br,
            terminalOpenArgs, Layouting::br,
            terminalExecuteArgs, Layouting::br,
        },
        buttons
    };
    // clang-format on

    QDialog *dialog = new QDialog(Utils::dialogParent());
    dialog->setWindowTitle(Tr::tr("Select Terminal Emulator"));
    dialog->setModal(true);
    layout.attachTo(dialog);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    dialog->show();
}

} // Utils

// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "terminalcommandaspect.h"

#include "commandline.h"
#include "elidinglabel.h"
#include "guiutils.h"
#include "hostosinfo.h"
#include "layoutbuilder.h"
#include "pathchooser.h"
#include "terminalcommand.h"
#include "utilstr.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QMenu>
#include <QPushButton>

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
}

void TerminalCommandAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    using namespace Layouting;

    auto detailLabel = new ElidingLabel;
    auto updateDetails = [this, detailLabel]() {
        const FilePath exe = terminalEmulator.expandedVolatileValue();
        detailLabel->setText(
            QString("%1: %2, %3: %4")
                .arg(msgTerminalHereAction())
                .arg(CommandLine(exe, terminalOpenArgs.volatileValue(), CommandLine::Raw)
                         .toUserOutput())
                .arg(Tr::tr("Run in Terminal"))
                .arg(CommandLine(exe, terminalExecuteArgs.volatileValue(), CommandLine::Raw)
                         .toUserOutput()));
    };
    updateDetails();
    connect(&terminalEmulator, &BaseAspect::volatileValueChanged, detailLabel, updateDetails);
    connect(&terminalOpenArgs, &BaseAspect::volatileValueChanged, detailLabel, updateDetails);
    connect(&terminalExecuteArgs, &BaseAspect::volatileValueChanged, detailLabel, updateDetails);

    QPushButton *changeButton = new QPushButton(Tr::tr("Customize..."));
    changeButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    QPushButton *presetButton = new QPushButton(Tr::tr("Presets"));
    auto presetMenu = new QMenu(presetButton);
    presetButton->setMenu(presetMenu);
    presetButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    for (const TerminalCommand &term : TerminalCommand::availableTerminalEmulators()) {
        QAction *action = presetMenu->addAction(term.command.toUserOutput());
        connect(action, &QAction::triggered, this, [this, term] {
            terminalEmulator.setVolatileValue(term.command.toUrlishString());
            terminalOpenArgs.setVolatileValue(term.openArgs);
            terminalExecuteArgs.setVolatileValue(term.executeArgs);
        });
    }

    connect(changeButton, &QPushButton::clicked, this, [this] {
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok);

        // clang-format off
        auto layout = Column {
            Form {
                terminalEmulator, br,
                terminalOpenArgs, br,
                terminalExecuteArgs, br,
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
    });

    addLabeledItem(parent, detailLabel);
    parent.addItem(changeButton);
    parent.addItem(presetButton);
}

} // Utils

// Copyright (C) 2016 AudioCodes Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "settingspage.h"

#include "clearcaseconstants.h"
#include "clearcaseplugin.h"
#include "clearcasesettings.h"
#include "clearcasetr.h"
#include <vcsbase/vcsbaseconstants.h>

#include <utils/aspects.h>
#include <utils/environment.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/pathchooser.h>
#include <utils/shutdownguard.h>

#include <QCoreApplication>

using namespace Utils;

namespace ClearCase::Internal {

class SettingsPageWidget final : public AspectContainer
{
public:
    SettingsPageWidget();

    void apply() final;

private:
    AspectContainer m_configuration{this};
    FilePathAspect m_command{&m_configuration};

    AspectContainer m_diff{this};
    TypedSelectionAspect<DiffType> m_diffType{&m_diff};
    StringAspect m_diffArgs{&m_diff};
    TextDisplay m_diffWarning{&m_diff};

    AspectContainer m_misc{this};
    IntegerAspect m_historyCount{&m_misc};
    IntegerAspect m_timeOut{&m_misc};
    BoolAspect m_autoCheckOut{&m_misc};
    BoolAspect m_autoAssignActivity{&m_misc};
    BoolAspect m_noComment{&m_misc};
    BoolAspect m_disableIndexer{&m_misc};
    StringAspect m_indexOnlyVOBs{&m_misc};

    bool m_extDiffAvailable = false;
};

SettingsPageWidget::SettingsPageWidget()
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ClearCase/ClearCaseSettingsPage.qml"));

    m_configuration.setQmlName("Configuration");
    m_configuration.setLabelText(Tr::tr("Configuration"));

    m_command.setQmlName("Command");
    m_command.setLabelText(Tr::tr("Command:"));
    m_command.setPromptDialogTitle(Tr::tr("ClearCase Command"));
    m_command.setExpectedKind(PathChooserKind::ExistingCommand);
    m_command.setHistoryCompleter("ClearCase.Command.History");

    m_diff.setQmlName("Diff");
    m_diff.setLabelText(Tr::tr("Diff"));

    m_diffType.setQmlName("DiffType");
    m_diffType.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
    m_diffType.addOption({Tr::tr("Graphical (single file only)"), {}, GraphicalDiff});
    m_diffType.addOption({Tr::tr("External"), {}, ExternalDiff});

    m_diffArgs.setQmlName("DiffArgs");
    m_diffArgs.setLabelText(Tr::tr("Arguments:"));
    m_diffArgs.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);

    m_diffWarning.setQmlName("DiffWarning");
    m_diffWarning.setIconType(InfoType::Warning);
    m_diffWarning.setWordWrap(true);

    m_misc.setQmlName("Misc");
    m_misc.setLabelText(Tr::tr("Miscellaneous"));

    m_historyCount.setQmlName("HistoryCount");
    m_historyCount.setLabelText(Tr::tr("History count:"));
    m_historyCount.setRange(0, 10000);

    m_timeOut.setQmlName("TimeOut");
    m_timeOut.setLabelText(Tr::tr("Timeout:"));
    m_timeOut.setRange(1, 360);
    m_timeOut.setSuffix(Tr::tr("s"));

    m_autoCheckOut.setQmlName("AutoCheckOut");
    m_autoCheckOut.setLabel(Tr::tr("Automatically check out files on edit"));
    m_autoCheckOut.setLabelPlacement(BoolAspect::LabelPlacement::Compact);

    m_autoAssignActivity.setQmlName("AutoAssignActivity");
    m_autoAssignActivity.setLabel(Tr::tr("Auto assign activity names"));
    m_autoAssignActivity.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    m_autoAssignActivity.setToolTip(Tr::tr("Check this if you have a trigger that renames "
        "the activity automatically. You will not be prompted for activity name."));

    m_noComment.setQmlName("NoComment");
    m_noComment.setLabel(Tr::tr("Do not prompt for comment during checkout or check-in"));
    m_noComment.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    m_noComment.setToolTip(Tr::tr("Check out or check in files with no comment "
                                  "(-nc, -ncomment)."));

    m_disableIndexer.setQmlName("DisableIndexer");
    m_disableIndexer.setLabel(Tr::tr("Disable indexer"));
    m_disableIndexer.setLabelPlacement(BoolAspect::LabelPlacement::Compact);

    m_indexOnlyVOBs.setQmlName("IndexOnlyVOBs");
    m_indexOnlyVOBs.setLabelText(Tr::tr("Index only VOBs:"));
    m_indexOnlyVOBs.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);
    m_indexOnlyVOBs.setToolTip(Tr::tr("VOBs list, separated by comma. Indexer will only traverse "
        "the specified VOBs. If left blank, all active VOBs will be indexed."));

    const ClearCaseSettings &s = settings();
    m_command.setValue(FilePath::fromString(s.ccCommand));
    m_timeOut.setValue(s.timeOutS);
    m_autoCheckOut.setValue(s.autoCheckOut);
    m_noComment.setValue(s.noComment);
    m_autoAssignActivity.setValue(s.autoAssignActivityName);
    m_historyCount.setValue(s.historyCount);
    m_disableIndexer.setValue(s.disableIndexer);
    m_diffArgs.setValue(s.diffArgs);
    m_indexOnlyVOBs.setValue(s.indexOnlyVOBs);

    // Behaviour, not layout: an external diff needs a "diff" to run.
    m_extDiffAvailable = !Environment::systemEnvironment().searchInPath("diff").isEmpty();
    if (m_extDiffAvailable) {
        m_diffWarning.setVisible(false);
    } else {
        QString diffWarning = Tr::tr("In order to use External diff, \"diff\" command needs to be "
                                     "accessible.");
        if (HostOsInfo::isWindowsHost()) {
            diffWarning += ' ';
            diffWarning.append(Tr::tr("DiffUtils is available for free download at "
                                      "http://gnuwin32.sourceforge.net/packages/diffutils.htm. "
                                      "Extract it to a directory in your PATH."));
        }
        m_diffWarning.setText(diffWarning);
        // Offering a choice that cannot be taken is worse than not offering it.
        if (std::optional<SelectionAspect::Option> option
            = m_diffType.optionForIndex(ExternalDiff)) {
            option->enabled = false;
            m_diffType.setOptionForIndex(ExternalDiff, *option);
        }
    }
    m_diffType.setValue(m_extDiffAvailable && s.diffType == ExternalDiff ? ExternalDiff
                                                                        : GraphicalDiff);

    // The arguments are only worth typing for the diff that takes them.
    const auto updateDiffArgsEnabled = [this] {
        m_diffArgs.setEnabled(m_diffType.volatileValue() == ExternalDiff);
    };
    updateDiffArgsEnabled();
    connect(&m_diffType, &BaseAspect::volatileValueChanged, this, updateDiffArgsEnabled);
}

void SettingsPageWidget::apply()
{
    AspectContainer::apply();

    ClearCaseSettings rc;
    rc.ccCommand = m_command().toUserOutput();
    rc.ccBinaryPath = m_command.expandedValue();
    rc.timeOutS = m_timeOut();
    rc.autoCheckOut = m_autoCheckOut();
    rc.noComment = m_noComment();
    rc.diffType = m_diffType();
    rc.autoAssignActivityName = m_autoAssignActivity();
    rc.historyCount = m_historyCount();
    rc.disableIndexer = m_disableIndexer();
    rc.diffArgs = m_diffArgs();
    rc.indexOnlyVOBs = m_indexOnlyVOBs();
    rc.extDiffAvailable = m_extDiffAvailable;

    setSettings(rc);
}

ClearCaseSettingsPage::ClearCaseSettingsPage()
{
    setId(ClearCase::Constants::VCS_ID_CLEARCASE);
    setDisplayName(Tr::tr("ClearCase"));
    setCategory(VcsBase::Constants::VCS_SETTINGS_CATEGORY);
    setSettingsProvider([] {
        static GuardedObject<SettingsPageWidget> theAspects;
        return theAspects.get();
    });
}

} // ClearCase::Internal

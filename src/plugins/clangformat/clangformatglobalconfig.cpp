// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangformatglobalconfig.h"

#include "clangformatconstants.h"
#include "clangformatsettings.h"
#include "clangformattr.h"
#include "clangformatutils.h"

#include <projectexplorer/project.h>
#include <projectexplorer/projecttree.h>
#include <texteditor/icodestylepreferences.h>
#include <utils/guiutils.h>

#include <limits>

using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace ClangFormat {

static bool projectClangFormatFileExists(const Project *project)
{
    llvm::Expected<clang::format::FormatStyle> styleFromProjectFolder = clang::format::getStyle(
        "file", project->projectFilePath().path().toStdString(), "none", "", nullptr, true);

    return styleFromProjectFolder && !(*styleFromProjectFolder == clang::format::getNoStyle());
}

ClangFormatGlobalConfig::ClangFormatGlobalConfig(Project *project, ICodeStylePreferences *codeStyle)
    : m_project(project)
    , m_codeStyle(codeStyle)
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ClangFormat/ClangFormatGlobalConfig.qml"));

    const QString sizeThresholdToolTip = Tr::tr(
        "Files greater than this will not be indented by ClangFormat.\n"
        "The built-in code indenter will handle indentation.");

    useGlobalSettings.setQmlName("UseGlobalSettings");
    useGlobalSettings.setLabel(Tr::tr("Use global settings"), BoolAspect::LabelPlacement::AtCheckBox);

    useClangFormat.setQmlName("UseClangFormat");
    useClangFormat.setLabel(Tr::tr("Use ClangFormat"), BoolAspect::LabelPlacement::AtCheckBox);

    fileSizeThreshold.setQmlName("FileSizeThreshold");
    fileSizeThreshold.setLabelText(Tr::tr("Ignore files greater than:"));
    fileSizeThreshold.setToolTip(sizeThresholdToolTip);
    fileSizeThreshold.setRange(1, std::numeric_limits<int>::max());
    fileSizeThreshold.setSuffix(" KB");

    formattingMode.setQmlName("FormattingMode");
    formattingMode.setLabelText(Tr::tr("Formatting mode:"));
    formattingMode.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    formattingMode.addOption(Tr::tr("Indenting only"));
    formattingMode.addOption(Tr::tr("Full formatting"));

    formatWhileTyping.setQmlName("FormatWhileTyping");
    formatWhileTyping.setLabel(Tr::tr("Format while typing"),
                               BoolAspect::LabelPlacement::AtCheckBox);

    formatOnSave.setQmlName("FormatOnSave");
    formatOnSave.setLabel(Tr::tr("Format edited code on file save"),
                          BoolAspect::LabelPlacement::AtCheckBox);

    customSettings.setQmlName("UseCustomSettings");
    customSettings.setLabel(Tr::tr("Use custom settings"), BoolAspect::LabelPlacement::AtCheckBox);
    customSettings.setToolTip(
        "<html>"
        + Tr::tr("When this option is enabled, ClangFormat will use a "
                 "user-specified configuration from the widget below, "
                 "instead of the project .clang-format file. You can "
                 "customize the formatting options for your code by "
                 "adjusting the settings in the widget. Note that any "
                 "changes made there will only affect the current "
                 "configuration, and will not modify the project "
                 ".clang-format file."));

    projectHasClangFormat.setQmlName("ProjectHasClangFormat");
    projectHasClangFormat.setText(
        Tr::tr("The current project has its own .clang-format file which "
               "can be overridden by the settings below."));

    projectFileNote.setQmlName("ProjectFileNote");
    projectFileNote.setIconType(InfoType::Warning);
    projectFileNote.setWordWrap(true);
    projectFileNote.setText(
        Tr::tr("Please note that the current project includes a .clang-format file, which will be "
               "used for code indenting and formatting."));

    m_customSettingsOnEntry = clangFormatSettings().useCustomSettings();
    m_projectHasOwnFile = m_project && projectClangFormatFileExists(m_project);

    const ClangFormatSettings::Mode currentMode
        = getProjectIndentationOrFormattingSettings(m_project);
    useClangFormat.setValue(currentMode != ClangFormatSettings::Mode::Disable);
    if (currentMode != ClangFormatSettings::Mode::Disable)
        formattingMode.setValue(currentMode);
    formatOnSave.setValue(clangFormatSettings().formatOnSave());
    formatWhileTyping.setValue(clangFormatSettings().formatWhileTyping());
    fileSizeThreshold.setValue(clangFormatSettings().fileSizeThreshold());
    customSettings.setValue(getProjectCustomSettings(m_project));

    // A project keeps its own mode and custom settings but reads the rest from
    // the global page, so the ones it cannot set are not shown at all.
    useGlobalSettings.setVisible(m_project != nullptr);
    if (m_project)
        useGlobalSettings.setValue(getProjectUseGlobalSettings(m_project));

    connect(&useClangFormat, &BaseAspect::volatileValueChanged, this, [this] {
        const ClangFormatSettings::Mode newMode = useClangFormat.volatileValue()
                                                      ? formattingMode.volatileValue()
                                                      : ClangFormatSettings::Mode::Disable;
        if (m_project)
            m_project->setNamedSettings(Constants::MODE_ID, static_cast<int>(newMode));
        updateVisibility();
        emit modeChanged(newMode);
    });

    connect(&formattingMode, &BaseAspect::volatileValueChanged, this, [this] {
        const ClangFormatSettings::Mode newMode = formattingMode.volatileValue();
        if (m_project)
            m_project->setNamedSettings(Constants::MODE_ID, static_cast<int>(newMode));
        updateEnabledState();
        updateProjectFileNote();
        emit modeChanged(newMode);
    });

    connect(&customSettings, &BaseAspect::volatileValueChanged, this, [this] {
        const bool checked = customSettings.volatileValue();
        if (m_project)
            m_project->setNamedSettings(Constants::USE_CUSTOM_SETTINGS_ID, checked);
        else
            clangFormatSettings().useCustomSettings.setValue(checked);
        updateProjectFileNote();
        emit useCustomSettingsChanged(checked);
    });

    if (m_project) {
        connect(&useGlobalSettings, &BaseAspect::volatileValueChanged, this, [this] {
            m_project->setNamedSettings(Constants::USE_GLOBAL_SETTINGS,
                                        useGlobalSettings.volatileValue());
            customSettings.setVolatileValue(getProjectCustomSettings(m_project));
            updateEnabledState();
            emit m_codeStyle->currentPreferencesChanged(m_codeStyle->currentPreferences());
            emit modeChanged(mode());
        });
    } else {
        // Global only: a project view writes through as it goes, so there is
        // nothing there for Apply to become enabled for.
        const QList<BaseAspect *> written{&useClangFormat,
                                          &formattingMode,
                                          &fileSizeThreshold,
                                          &formatWhileTyping,
                                          &formatOnSave,
                                          &customSettings};
        for (BaseAspect *aspect : written)
            installMarkSettingsDirtyTrigger(aspect);
    }

    updateVisibility();
    updateEnabledState();
}

void ClangFormatGlobalConfig::updateVisibility()
{
    // Everything below the check box describes how ClangFormat should behave,
    // so none of it means anything while ClangFormat is off.
    const bool on = useClangFormat.volatileValue();

    formattingMode.setVisible(on);
    customSettings.setVisible(on);
    fileSizeThreshold.setVisible(on && !m_project);
    formatWhileTyping.setVisible(on && !m_project);
    formatOnSave.setVisible(on && !m_project);
    projectHasClangFormat.setVisible(on && m_projectHasOwnFile);

    updateProjectFileNote();
}

void ClangFormatGlobalConfig::updateEnabledState()
{
    const bool useGlobal = m_project && useGlobalSettings.volatileValue();

    useClangFormat.setEnabled(!useGlobal);
    formattingMode.setEnabled(!useGlobal);
    fileSizeThreshold.setEnabled(!useGlobal);
    customSettings.setEnabled(!useGlobal);

    // These two ask ClangFormat to rewrite code rather than only indent it, so
    // they are only a choice when it is formatting.
    const bool isFormatting = formattingMode.volatileValue()
                              == ClangFormatSettings::Mode::Formatting;
    formatWhileTyping.setEnabled(!useGlobal && isFormatting);
    formatOnSave.setEnabled(!useGlobal && isFormatting);
}

void ClangFormatGlobalConfig::updateProjectFileNote()
{
    if (!useClangFormat.volatileValue()) {
        projectFileNote.setVisible(false);
        return;
    }

    // Without a project of its own the global page still warns about the
    // project the user is looking at, which is what the widget form did.
    const Project *project = m_project ? m_project : ProjectTree::currentProject();
    bool show = false;
    if (project) {
        const FilePath dir = project->projectDirectory();
        const bool hasFile = (dir / Constants::SETTINGS_FILE_NAME).exists()
                             || (dir / Constants::SETTINGS_FILE_ALT_NAME).exists();
        show = hasFile && !customSettings.volatileValue();
    }
    projectFileNote.setVisible(show);
}

void ClangFormatGlobalConfig::apply()
{
    AspectContainer::apply();

    // All of these are global. The project view shows them but does not own
    // them, so writing from there would rewrite the global settings with their
    // own contents.
    if (m_project)
        return;

    ClangFormatSettings &settings = clangFormatSettings();
    settings.formatOnSave.setValue(formatOnSave());
    settings.formatWhileTyping.setValue(formatWhileTyping());
    settings.mode.setValue(mode());
    settings.useCustomSettings.setValue(customSettings());
    settings.fileSizeThreshold.setValue(fileSizeThreshold());
    m_customSettingsOnEntry = customSettings();
    settings.writeSettings();
}

void ClangFormatGlobalConfig::cancel()
{
    AspectContainer::cancel();

    // Only the global view changes the global "use custom settings" as the user
    // types, so only it has something to put back.
    if (m_project)
        return;
    clangFormatSettings().useCustomSettings.setValue(m_customSettingsOnEntry);
}

ClangFormatSettings::Mode ClangFormatGlobalConfig::mode() const
{
    if (m_project && useGlobalSettings.volatileValue())
        return clangFormatSettings().mode();
    if (!useClangFormat.volatileValue())
        return ClangFormatSettings::Mode::Disable;
    return formattingMode.volatileValue();
}

bool ClangFormatGlobalConfig::useCustomSettings() const
{
    return customSettings.volatileValue();
}

} // namespace ClangFormat

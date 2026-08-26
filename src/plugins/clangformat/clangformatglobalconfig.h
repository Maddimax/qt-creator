// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "clangformatsettings.h"

#include <utils/aspects.h>

namespace ProjectExplorer { class Project; }
namespace TextEditor { class ICodeStylePreferences; }

namespace ClangFormat {

// The ClangFormat settings shown above a code style, either globally or for one
// project. The two list the same things; they differ in what they may change.
// A project keeps its own mode and its own "use custom settings", and may defer
// to the global ones instead - so the settings a project does not own are
// hidden rather than shown disabled, which is what the widget form did.
class ClangFormatGlobalConfig final : public Utils::AspectContainer
{
    Q_OBJECT

public:
    ClangFormatGlobalConfig(ProjectExplorer::Project *project,
                            TextEditor::ICodeStylePreferences *codeStyle);

    void apply() override;
    void cancel() override;

    ClangFormatSettings::Mode mode() const;
    bool useCustomSettings() const;

    Utils::BoolAspect useGlobalSettings{this};
    Utils::BoolAspect useClangFormat{this};
    Utils::IntegerAspect fileSizeThreshold{this};
    Utils::TypedSelectionAspect<ClangFormatSettings::Mode> formattingMode{this};
    Utils::BoolAspect formatWhileTyping{this};
    Utils::BoolAspect formatOnSave{this};
    Utils::BoolAspect customSettings{this};
    Utils::TextDisplay projectHasClangFormat{this};
    Utils::TextDisplay projectFileNote{this};

signals:
    void modeChanged(ClangFormatSettings::Mode newMode);
    void useCustomSettingsChanged(bool doUse);

private:
    void updateVisibility();
    void updateEnabledState();
    void updateProjectFileNote();

    ProjectExplorer::Project *m_project = nullptr;
    TextEditor::ICodeStylePreferences *m_codeStyle = nullptr;
    bool m_customSettingsOnEntry = false;
    // Asking ClangFormat costs a style lookup, so it is asked once.
    bool m_projectHasOwnFile = false;
};

} // namespace ClangFormat

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codestylesettingspropertiespage.h"

#include "editorconfiguration.h"
#include "project.h"
#include "projectexplorertr.h"
#include "projectpanelfactory.h"

#include <cppeditor/cppeditorconstants.h>

#include <texteditor/icodestylepreferencesfactory.h>

#include <coreplugin/icore.h>

#include <utils/aspects.h>

using namespace TextEditor;

namespace ProjectExplorer::Internal {

// What the panel shows: one form per language, and which language is being
// looked at. The languages are not known here - each factory is asked what it
// has - so the page repeats its form over them rather than naming any.
class CodeStyleProjectPanel final : public Utils::AspectContainer
{
public:
    explicit CodeStyleProjectPanel(Project *project)
    {
        // Before registering: insertAspect() forces the container's own
        // auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(
            QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/CodeStyleProjectPanel.qml"));

        m_globalLink.setQmlName("GlobalLink");
        m_globalLink.setTextFormat(Utils::AspectControls::TextFormat::RichText);
        m_globalLink.setText("<a href=\"page\">" + Tr::tr("Global settings") + "</a>");
        connect(&m_globalLink, &Utils::TextDisplay::linkActivated, this, [] {
            Core::ICore::showSettings(CppEditor::Constants::CPP_CODE_STYLE_SETTINGS_ID);
        });
        registerAspect(&m_globalLink);

        m_language.setQmlName("Language");
        m_language.setLabelText(Tr::tr("Language:"));
        m_language.setDisplayStyle(Utils::SelectionAspect::DisplayStyle::ComboBox);
        registerAspect(&m_language);

        m_forms.setQmlName("Forms");
        const EditorConfiguration * const config = project->editorConfiguration();
        for (ICodeStylePreferencesFactory * const factory : codeStyleFactories()) {
            ICodeStylePreferences * const codeStyle = config->codeStyle(factory->languageId());
            Utils::AspectContainer * const form
                = factory->createProjectAspects(project->projectFilePath(), codeStyle);
            if (!form)
                continue;
            m_language.addOption(factory->displayName());
            m_forms.registerAspect(form, /*takeOwnership=*/true);
        }
        registerAspect(&m_forms);

        // Behaviour, not layout: one language at a time, which the stacked
        // widget did by index.
        showCurrentLanguage();
        m_language.addOnChanged(this, [this] { showCurrentLanguage(); });
    }

    static Utils::Key extraDataKey() { return "CodeStyleProjectPanel"; }

private:
    void showCurrentLanguage()
    {
        const int current = m_language();
        int index = 0;
        for (Utils::BaseAspect * const form : m_forms.aspects())
            form->setVisible(index++ == current);
    }

    Utils::TextDisplay m_globalLink;
    Utils::SelectionAspect m_language;
    Utils::AspectContainer m_forms;
};

static CodeStyleProjectPanel *codeStyleProjectPanel(Project *project)
{
    const Utils::Key key = CodeStyleProjectPanel::extraDataKey();
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new CodeStyleProjectPanel(project));
        project->setExtraData(key, v);
    }
    return v.value<CodeStyleProjectPanel *>();
}

class CodeStyleProjectPanelFactory final : public ProjectPanelFactory
{
public:
    CodeStyleProjectPanelFactory()
    {
        setPriority(40);
        setDisplayName(Tr::tr("Code Style"));
        setSettingsProvider([](Project *project) { return codeStyleProjectPanel(project); });
    }
};

void setupCodeStyleProjectPanel()
{
    static CodeStyleProjectPanelFactory theCodeStyleProjectPanelFactory;
}

} // ProjectExplorer::Internal

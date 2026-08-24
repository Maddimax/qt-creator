// Copyright (C) Filippo Cucchetto <filippocucchetto@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "nimcodestylesettingspage.h"

#include "../editor/nimindenter.h"
#include "../nimconstants.h"
#include "../nimtr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <texteditor/codestyleeditor.h>
#include <texteditor/codestylepool.h>
#include <texteditor/icodestylepreferencesfactory.h>
#include <texteditor/icodestylepreferences.h>
#include <texteditor/indenter.h>
#include <texteditor/tabsettings.h>

#include <utils/aspects.h>
#include <utils/id.h>
#include <utils/shutdownguard.h>

#include <QTextDocument>

using namespace TextEditor;
using namespace Utils;

namespace Nim {

// What the Nim Code Style form edits, which is the tab settings and nothing
// else: the language has no settings of its own. The selector and the preview
// around it are the hosting CodeStyleAspect's.
class NimCodeStyleAspects final : public AspectContainer
{
public:
    explicit NimCodeStyleAspects(ICodeStylePreferences *preferences)
    {
        m_tabSettings.setQmlName("TabSettings");
        registerAspect(&m_tabSettings);
        // TabSettings writes through to the preferences the page handed over -
        // its own editable copy - so there is nothing to apply here.
        m_tabSettings.setPreferences(preferences);
    }

private:
    TabSettings m_tabSettings;
};

// NimCodeStylePreferencesFactory

class NimCodeStylePreferencesFactory final : public ICodeStylePreferencesFactory
{
public:
    NimCodeStylePreferencesFactory()
        : ICodeStylePreferencesFactory(Constants::C_NIMLANGUAGE_ID)
    {
        setDisplayName(Tr::tr(Constants::C_NIMLANGUAGE_NAME));
        setSnippetGroupId(Constants::C_NIMSNIPPETSGROUP_ID);
        setPreviewText(QString::fromLatin1(Constants::C_NIMCODESTYLEPREVIEWSNIPPET));
        setIndenterCreator([](QTextDocument *doc) { return createNimIndenter(doc); });
        setCodeStyleCreator([] {
            auto prefs = new ICodeStylePreferences;
            prefs->setSettingsSuffix("TabPreferences");
            return prefs;
        });
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Nim/NimCodeStylePage.qml"));
        setSettingsAspectsCreator([](ICodeStylePreferences *codeStyle,
                                     CodeStylePreviewAspect *) {
            return new NimCodeStyleAspects(codeStyle);
        });

        setGlobalCodeStyleId(Constants::C_NIMGLOBALCODESTYLE_ID);
        setDefaultCodeStyleId("nim");
        setBuiltInCodeStyles([this](CodeStylePool *pool) {
            // Built-in, read-only "Nim" style.
            TabSettingsData tabSettings;
            tabSettings.m_tabPolicy = TabSettingsData::SpacesOnlyTabPolicy;
            tabSettings.m_tabSize = 2;
            tabSettings.m_indentSize = 2;
            tabSettings.m_continuationAlignBehavior = TabSettingsData::ContinuationAlignWithIndent;

            m_nimCodeStyle.setId("nim");
            m_nimCodeStyle.setDisplayName(Tr::tr("Nim"));
            m_nimCodeStyle.setReadOnly(true);
            m_nimCodeStyle.setTabSettings(tabSettings);
            pool->addCodeStyle(&m_nimCodeStyle);
        });
        setupCodeStyles();

        registerMimeTypeForLanguageId(Constants::C_NIM_MIMETYPE, Constants::C_NIMLANGUAGE_ID);
        registerMimeTypeForLanguageId(Constants::C_NIM_SCRIPT_MIMETYPE, Constants::C_NIMLANGUAGE_ID);
    }

private:
    ICodeStylePreferences m_nimCodeStyle;
};

// NimCodeStyleSettingsPage

class NimCodeStyleSettingsPage final : public Core::IOptionsPage
{
public:
    NimCodeStyleSettingsPage()
    {
        setId(Constants::C_NIMCODESTYLESETTINGSPAGE_ID);
        setDisplayName(Tr::tr("Code Style"));
        setCategory(Constants::C_NIMCODESTYLESETTINGSPAGE_CATEGORY);
        setSettingsProvider([] {
            static CodeStyleAspect settings(
                codeStyleFactory(Constants::C_NIMLANGUAGE_ID)->globalCodeStyle(),
                Constants::C_NIMLANGUAGE_ID);
            return &settings;
        });
    }
};

void Internal::setupNimCodeStyle()
{
    static GuardedObject<NimCodeStylePreferencesFactory> theNimCodeStylePreferencesFactory;
    static GuardedObject<NimCodeStyleSettingsPage> theNimCodeStyleSettingsPage;
}

} // Nim

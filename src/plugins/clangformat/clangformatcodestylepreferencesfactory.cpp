// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangformatcodestylepreferencesfactory.h"

#include "clangformatconstants.h"
#include "clangformatfile.h"
#include "clangformatglobalconfig.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include "clangformatindenter.h"
#include "clangformatsettings.h"
#include "clangformattr.h"
#include "clangformatutils.h"

#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/editormanager/ieditorfactory.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>

#include <cppeditor/cppcodestylesettings.h>
#include <cppeditor/cppcodestylesettingspage.h>
#include <cppeditor/cppcodestylesnippets.h>
#include <cppeditor/cppeditorconstants.h>
#include <cppeditor/cpphighlighter.h>

#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/editorconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <texteditor/codestyleeditor.h>
#include <texteditor/codestylepool.h>
#include <texteditor/codestyleselectorwidget.h>
#include <texteditor/displaysettings.h>
#include <texteditor/fontsettings.h>
#include <texteditor/icodestylepreferences.h>
#include <texteditor/icodestylepreferencesfactory.h>
#include <texteditor/indenter.h>
#include <texteditor/snippets/snippeteditor.h>
#include <texteditor/snippets/snippetprovider.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/filedialogs.h>
#include <utils/filepath.h>
#include <utils/fileutils.h>
#include <utils/guard.h>
#include <utils/guiutils.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/shutdownguard.h>

#include <QComboBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QString>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace ClangFormat {


// What the text in the .clang-format editor would do, or why it would not.
// The editor holds unsaved text, so both the preview and the warning follow
// from *that* rather than from the file on disk.
static Result<> checkStyleText(const QString &text, clang::format::FormatStyle &style)
{
    return parseConfigurationContent(text.toStdString(), style);
}

// The .clang-format file being edited, the preview of what it does, and
// whether either may be typed into. The two editors belong to the form; this
// carries what they need and the rules tying them together.
class ClangFormatCodeStyleAspects final : public AspectContainer
{
public:
    ClangFormatCodeStyleAspects(ICodeStylePreferences *codeStyle,
                                CodeStylePreviewAspect *preview)
        : m_globalSettings(nullptr, codeStyle)
        , m_preview(preview)
        , m_config(std::make_unique<ClangFormatFile>(codeStyle->currentPreferences()))
    {
        // The page shows the global block above the style, the way the widget
        // editor did; naming it is what lets the form reach it.
        m_globalSettings.setQmlName("Global");
        registerAspect(&m_globalSettings);

        // Which file the form opens. Invisible because it is not something to
        // set - it follows the style being edited.
        styleFilePath.setQmlName("StyleFilePath");
        styleFilePath.setVisible(false);

        styleText.setQmlName("StyleText");
        styleText.setVisible(false);
        styleText.addOnChanged(this, [this] { styleTextChanged(); });

        clangVersion.setQmlName("ClangVersion");
        clangVersion.setText(
            Tr::tr("Current ClangFormat version: %1.").arg(LLVM_VERSION_STRING));

        fileProblem.setQmlName("FileProblem");
        fileProblem.setIconType(InfoType::Warning);
        fileProblem.setVisible(false);

        // Whether the file may be typed into. A read-only style, or custom
        // settings turned off, means the form shows it and nothing more.
        editable.setQmlName("Editable");
        editable.setVisible(false);

        connect(codeStyle, &ICodeStylePreferences::currentPreferencesChanged,
                this, &ClangFormatCodeStyleAspects::followCodeStyle);
        connect(codeStyle, &ICodeStylePreferences::aboutToBeRemoved,
                this, &ClangFormatFile::removeClangFormatFileForStylePreferences);
        connect(codeStyle, &ICodeStylePreferences::aboutToBeCopied,
                this, &ClangFormatFile::copyClangFormatFileBasedOnStylePreferences);
        connect(&m_globalSettings, &ClangFormatGlobalConfig::useCustomSettingsChanged,
                this, [this] { updateEditable(); });

        followCodeStyle(codeStyle->currentPreferences());
    }

    void apply() override
    {
        AspectContainer::apply();
        // Only what the user could have typed: a read-only style must not be
        // written back over.
        if (editable())
            m_config->filePath().writeFileContents(styleText().toUtf8());
    }

    // The .clang-format file's text. The form edits this, so the warning and
    // the preview are about what is on screen rather than what is on disk.
    Utils::StringAspect styleText{this};
    Utils::StringAspect styleFilePath{this};
    Utils::TextDisplay clangVersion{this};
    Utils::TextDisplay fileProblem{this};
    Utils::BoolAspect editable{this};

private:
    void followCodeStyle(ICodeStylePreferences *codeStyle)
    {
        if (!codeStyle)
            return;
        m_config.reset(new ClangFormatFile(codeStyle));
        m_config->setIsReadOnly(codeStyle->isReadOnly());
        styleFilePath.setValue(m_config->filePath().toUrlishString());
        // The file the style points at, not the one that was being edited:
        // switching style has to show the new file's contents.
        styleText.setValue(
            QString::fromUtf8(m_config->filePath().fileContents().value_or(QByteArray())));
        updateEditable();
    }

    // What the text on screen would do, or why it would do nothing.
    void styleTextChanged()
    {
        clang::format::FormatStyle parsed{};
        const Result<> ok = checkStyleText(styleText.volatileValue(), parsed);
        fileProblem.setVisible(!ok);
        if (!ok) {
            fileProblem.setText(Tr::tr("Warning:") + " " + ok.error());
            return;
        }
        // The preview re-indents with what was just typed rather than with what
        // was last saved - that is the whole point of showing it.
        if (m_preview)
            m_preview->formatText();
    }

    void updateEditable()
    {
        editable.setValue(!m_config->isReadOnly() && m_globalSettings.useCustomSettings());
    }

    ClangFormatGlobalConfig m_globalSettings;
    CodeStylePreviewAspect *m_preview = nullptr;
    std::unique_ptr<ClangFormatFile> m_config;
};

class ClangFormatCodeStylePreferencesFactory final : public ICodeStylePreferencesFactory
{
public:
    ClangFormatCodeStylePreferencesFactory()
        : ICodeStylePreferencesFactory(CppEditor::Constants::CPP_SETTINGS_ID)
    {
        setDisplayName(Tr::tr("C++"));
        setSnippetGroupId(CppEditor::Constants::CPP_SNIPPETS_GROUP_ID);
        setPreviewText(QString::fromLatin1(CppEditor::Constants::DEFAULT_CODE_STYLE_SNIPPETS[0]));
        setIndenterCreator([](QTextDocument *doc) { return new ClangFormatForwardingIndenter(doc); });
        setCodeStyleCreator([] { return new CppEditor::CppCodeStylePreferences; });
        setSettingsAspectsCreator(
            [](ICodeStylePreferences *codeStyle, CodeStylePreviewAspect *preview) {
                return new ClangFormatCodeStyleAspects(codeStyle, preview);
            });
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ClangFormat/ClangFormatCodeStylePage.qml"));
    }
};

void setupCodeStyleFactory()
{
    // Replace the default one by overwriting it with this here which has the same ID.
    static GuardedObject<ClangFormatCodeStylePreferencesFactory> theClangFormatStyleFactory;
}

} // namespace ClangFormat

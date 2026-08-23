// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "highlightersettings.h"

#include "highlighterhelper.h"
#include "texteditorconstants.h"
#include "texteditortr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/algorithm.h>
#include <utils/pathvalidation.h>
#include <QDesktopServices>

using namespace Utils;

namespace TextEditor {

HighlighterSettings &globalHighlighterSettings()
{
    static HighlighterSettings theHighlighterSettings;
    return theHighlighterSettings;
}

HighlighterSettings::HighlighterSettings()
{
    setAutoApply(false);

    setSettingsGroup(QString("Text") + Constants::HIGHLIGHTER_SETTINGS_CATEGORY);

    definitionFilesPath.setSettingsKey("UserDefinitionFilesPath");
    definitionFilesPath.setExpectedKind(PathChooserKind::ExistingDirectory);
    definitionFilesPath.setHistoryCompleter("TextEditor.Highlighter.History");
    const FilePath path = Core::ICore::userResourcePath("generic-highlighter");
    if (path.exists() || path.ensureWritableDir())
        definitionFilesPath.setDefaultPathValue(path);

    skipUpdateCheckForFilesPattern.setSettingsKey("skipUpdateCheckForFilesPatterns");
    skipUpdateCheckForFilesPattern.setLabelText(Tr::tr("Skip update check for files matching:"));
    skipUpdateCheckForFilesPattern.setDisplayStyle(
        StringListAspect::DisplayStyle::CommaSeparatedLineEdit);
    skipUpdateCheckForFilesPattern.setDefaultValue(
        {"*.txt", "LICENSE*", "README", "INSTALL", "COPYING", "NEWS", "qmldir"});

    skipFilesPattern.setSettingsKey("skipFilesPatterns");
    skipFilesPattern.setLabelText(Tr::tr("Skip syntax highlighting for files matching:"));
    skipFilesPattern.setDisplayStyle(StringListAspect::DisplayStyle::CommaSeparatedLineEdit);
    skipFilesPattern.setDefaultValue({});
    connect(&skipFilesPattern, &StringListAspect::changed, this, []{ HighlighterHelper::reload(); });

    engineNote.setText(
        Tr::tr("Highlight definitions are provided by the %1 engine.")
            .arg("<a href=\"https://invent.kde.org/frameworks/syntax-highlighting\">"
                 "KSyntaxHighlighting</a>"));
    engineNote.setWordWrap(true);
    engineNote.setQmlName("EngineNote");
    QObject::connect(
        &engineNote, &Utils::TextDisplay::linkActivated, &engineNote, [](const QString &link) {
            QDesktopServices::openUrl(QUrl(link));
        });

    userFilesLabel.setText(Tr::tr("User Highlight Definition Files"));
    userFilesLabel.setQmlName("UserFilesLabel");

    updateStatus.setQmlName("UpdateStatus");

    downloadDefinitions.setActionText(Tr::tr("Download Definitions"));
    downloadDefinitions.setToolTip(
        Tr::tr("Download missing and update existing syntax definition files."));
    downloadDefinitions.setQmlName("DownloadDefinitions");
    downloadDefinitions.setAction([this] {
        HighlighterHelper::downloadDefinitions(
            [this](const QString &message) { updateStatus.setText(message); });
    });

    reloadDefinitions.setActionText(Tr::tr("Reload Definitions"));
    reloadDefinitions.setToolTip(Tr::tr("Reload externally modified definition files."));
    reloadDefinitions.setQmlName("ReloadDefinitions");
    reloadDefinitions.setAction([] { HighlighterHelper::reload(); });

    resetRememberedDefinitions.setActionText(Tr::tr("Reset Remembered Definitions"));
    resetRememberedDefinitions.setToolTip(
        Tr::tr("Reset definitions remembered for files that can be associated with more "
               "than one highlighter definition."));
    resetRememberedDefinitions.setQmlName("ResetRememberedDefinitions");
    resetRememberedDefinitions.setAction(
        [] { HighlighterHelper::clearDefinitionForDocumentCache(); });

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/HighlighterSettingsPage.qml"));

    readSettings();
}

static bool matchesPattern(const QString &fileName, const QStringList &patterns)
{
    return Utils::anyOf(patterns, [&fileName](const QString &pattern) {
        QRegularExpression regExp(QRegularExpression::wildcardToRegularExpression(pattern),
                                  QRegularExpression::CaseInsensitiveOption);
        return fileName.indexOf(regExp) != -1;
    });
}

bool HighlighterSettings::skipUpdateCheck(const QString &fileName) const
{
    return matchesPattern(fileName, skipUpdateCheckForFilesPattern());
}

bool HighlighterSettings::skipHighlighting(const QString &fileName) const
{
    return matchesPattern(fileName, skipFilesPattern());
}

// HighlighterSettingsPage

class HighlighterSettingsPage final : public Core::IOptionsPage
{
public:
    HighlighterSettingsPage()
    {
        setId(Constants::TEXT_EDITOR_HIGHLIGHTER_SETTINGS);
        setDisplayName(Tr::tr("Generic Highlighter"));
        setCategory(TextEditor::Constants::TEXT_EDITOR_SETTINGS_CATEGORY);
        setSettingsProvider([] { return &globalHighlighterSettings(); });
    }
};

const static HighlighterSettingsPage theHighlighterSettingsPage;

} // TextEditor

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>
#include <utils/filepath.h>

namespace QmlJSEditor::Internal {

class AnalyzerMessagesAspectPrivate;

// Which static analyzer messages are on, and which of those are off in files
// that are not a Qt Quick UI. One row per known message; the check states are
// the aspect's, so isDirty() and cancel() do not need a page to be open.
// Persisted under the legacy DISABLED_MESSAGES / DISABLED_MESSAGES_NONQUICKUI
// keys, which QmlJS::Check reads directly.
class AnalyzerMessagesAspect final : public Utils::BaseAspect
{
    Q_OBJECT

public:
    AnalyzerMessagesAspect(Utils::AspectContainer *container = nullptr);
    ~AnalyzerMessagesAspect() final;

    void apply() final;
    void cancel() final;
    bool isDirty() const final;
    void readSettings() final;
    void writeSettings() const final;

    Utils::AspectPresentation presentation() const override;
    QAbstractItemModel *tableModel() override;

    void resetToDefault();

private:
    AnalyzerMessagesAspectPrivate *d = nullptr;
};

class QmlJsEditingSettings final : public Utils::AspectContainer
{
public:
    QmlJsEditingSettings();

    Utils::FilePath defaultQdsCommand() const;

    Utils::BoolAspect enableContextPane{this};
    Utils::BoolAspect pinContextPane{this};
    Utils::BoolAspect autoFormatOnSave{this};
    Utils::BoolAspect autoFormatOnlyCurrentProject{this};
    Utils::BoolAspect foldAuxData{this};
    Utils::BoolAspect useCustomAnalyzer{this};
    Utils::SelectionAspect uiQmlOpenMode{this};
    AnalyzerMessagesAspect analyzerMessages{this};
    Utils::ActionAspect resetAnalyzerMessages{this};
    Utils::TextDisplay qdsHint{this};
    Utils::FilePathAspect qdsCommand{this};
    Utils::ActionAspect qdsInstall{this};
    Utils::ActionAspect openLanguageServerSettings{this};
};

QmlJsEditingSettings &settings();

void setupQmlJsEditingSettings();

} // QmlJSEditor::Internal

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppcodestylesettings.h"

#include <utils/result.h>

#include <QWidget>

namespace TextEditor {
class CodeStylePreviewAspect;
class ICodeStylePreferences;
}
namespace Utils { class AspectContainer; }

namespace CppEditor {

class CppCodeStylePreferencesWidgetPrivate;

// The C++ code style settings drawn as a widget, for ClangFormat's legacy
// indenter panel. The settings page itself is drawn with Qt Quick from the same
// aspects; this is the second renderer, not a second definition.
class CPPEDITOR_EXPORT CppCodeStylePreferencesWidget : public QWidget
{
public:
    explicit CppCodeStylePreferencesWidget(CppCodeStylePreferences *codeStylePreferences);
    ~CppCodeStylePreferencesWidget() override;

    void apply();
    void cancel();

private:
    CppCodeStylePreferencesWidgetPrivate *d = nullptr;
};

namespace Internal {

void setupCppCodeStyleSettings();

// The aspects the Code Style form edits, for the page-local copy of the
// preferences. See ICodeStylePreferencesFactory::setSettingsAspectsCreator().
Utils::AspectContainer *createCppCodeStyleAspects(TextEditor::ICodeStylePreferences *codeStyle,
                                                  TextEditor::CodeStylePreviewAspect *preview);
Utils::Result<QString> formatCppPreview(TextEditor::ICodeStylePreferences *codeStyle,
                                        const QString &text);

} // namespace Internal

} // namespace CppEditor

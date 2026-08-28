// Copyright (C) 2016 Nicolas Arnaud-Cormos
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "imacrohandler.h"

namespace Core { class IEditor; }



namespace Macros::Internal {

class TextEditorMacroHandler : public IMacroHandler
{
public:
    TextEditorMacroHandler();

    void startRecording(Macro *macro) override;
    void endRecordingMacro(Macro *macro) override;

    bool canExecuteEvent(const MacroEvent &macroEvent) override;
    bool executeEvent(const MacroEvent &macroEvent) override;

    bool eventFilter(QObject *watched, QEvent *event) override;

    void changeEditor(Core::IEditor *editor);
    void closeEditor(Core::IEditor *editor);

private:
    // Whatever is showing a text document. Only its widget is ever used, to
    // watch keystrokes and to replay them, and that is on Core::IEditor - so
    // there is nothing here for a narrower type to add.
    Core::IEditor *m_currentEditor = nullptr;
};

} // namespace Macros::Internal

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

namespace Core { class IEditor; }

namespace TextEditor {

class TextViewport;

namespace Internal {

// The view inside \a editor, when \a editor is the Quick editor at all.
// What the free functions handing out a view's relay objects dispatch on.
TextViewport *viewportForEditor(Core::IEditor *editor);

// The Qt Quick code editor, offered beside the widget one rather than instead
// of it: it is not finished, so it must not become what a text file opens in.
void setupQuickTextEditor();

#ifdef WITH_TESTS
QObject *createQuickTextEditorTest();
#endif

} // namespace Internal
} // namespace TextEditor

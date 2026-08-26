// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

namespace TextEditor::Internal {

// The Qt Quick code editor, offered beside the widget one rather than instead
// of it: it is not finished, so it must not become what a text file opens in.
void setupQuickTextEditor();

#ifdef WITH_TESTS
QObject *createQuickTextEditorTest();
#endif

} // namespace TextEditor::Internal

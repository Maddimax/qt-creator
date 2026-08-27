// Copyright (C) Filippo Cucchetto <filippocucchetto@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/texteditor.h>

namespace Nim {
namespace Suggest { class NimSuggestClientRequest; }

class NimTextEditorWidget : public TextEditor::TextEditorWidget
{
public:
    NimTextEditorWidget(QWidget* parent = nullptr);
};

// Where the symbol under the cursor is defined, for the editor factory.
TextEditor::TextEditorFactory::LinkFinder nimLinkFinder();

}

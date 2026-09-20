// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#pragma once

#include <coreplugin/editormanager/ieditor.h>
#include <texteditor/textdocument.h>
#include <utils/uniqueobjectptr.h>

namespace EffectComposer {

class EffectComposerUniformsTableModel;

struct ShaderEditorData
{
    using Creator = std::function<
        ShaderEditorData *(const QString &fragmentShader, const QString &vertexShader)>;

    EffectComposerUniformsTableModel *tableModel = nullptr;
    std::function<QStringList()> uniformsCallback = nullptr;
    std::function<void()> exitEditorCallback = nullptr;

    TextEditor::TextDocumentPtr fragmentDocument;
    TextEditor::TextDocumentPtr vertexDocument;

    ~ShaderEditorData()
    {
        if (exitEditorCallback)
            exitEditorCallback();
    }

private:
    friend class EffectShadersCodeEditor;
    // An editor each, whose widget the code window stacks; the widget goes
    // with the editor.
    Utils::UniqueObjectLatePtr<Core::IEditor> fragmentEditor;
    Utils::UniqueObjectLatePtr<Core::IEditor> vertexEditor;

    ShaderEditorData() = default;
};

} // namespace EffectComposer

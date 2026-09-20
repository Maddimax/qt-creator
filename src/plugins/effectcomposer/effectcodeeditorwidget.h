// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/texteditor.h>
#include <qmljseditor/qmljseditordocument.h>

#include <functional>

namespace QmlJSEditor {
class SemanticHighlighter;
}

namespace EffectComposer {

// A shader being edited in the Effect Composer's code window: a QML/JS
// document of its own whose completion offers the effect's uniforms too.
// The window shows it in the Qt Quick editor; what a widget subclass used to
// answer for completion is the document's now, so either view has it.
class EffectDocument : public QmlJSEditor::QmlJSEditorDocument
{
    Q_OBJECT

public:
    EffectDocument();
    ~EffectDocument();

    // The uniforms of the effect, for completion; asked when completing.
    void setUniformsCallback(const std::function<QStringList()> &callback);
    QStringList uniforms() const;

    std::unique_ptr<TextEditor::AssistInterface> createAssistInterface(
        const QTextCursor &cursor, TextEditor::AssistKind kind, TextEditor::AssistReason reason,
        Core::IEditor *editor = nullptr) const override;

protected:
    void applyFontSettings() final;
    void triggerPendingUpdates() final;

private:
    QmlJSEditor::SemanticHighlighter *m_semanticHighlighter = nullptr;
    std::function<QStringList()> m_getUniforms;
};

class EffectCodeEditorFactory : public TextEditor::TextEditorFactory
{
public:
    EffectCodeEditorFactory();
};

#ifdef WITH_TESTS
QObject *createEffectCodeEditorTest();
#endif

} // namespace EffectComposer

// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"
#include "cppsemanticinfo.h"

#include <cplusplus/CppDocument.h>

#include <texteditor/blockrange.h>
#include <texteditor/refactoroverlay.h>
#include <texteditor/textdocument.h>

#include <QTextEdit>

namespace ProjectExplorer { class Node; }

namespace CppEditor {

namespace Internal {
class OutlineModel;
class ParseContextModel;
}

class CursorInfo;
class CursorInfoParams;

class CPPEDITOR_EXPORT CppEditorDocument : public TextEditor::TextDocument
{
    Q_OBJECT

    friend class CppEditorDocumentHandleImpl;

public:
    explicit CppEditorDocument();
    ~CppEditorDocument() override;

    bool isObjCEnabled() const;
    void setCompletionAssistProvider(TextEditor::CompletionAssistProvider *provider) override;
    TextEditor::CompletionAssistProvider *completionAssistProvider() const override;
    TextEditor::IAssistProvider *quickFixAssistProvider() const override;

    void recalculateSemanticInfoDetached();
    SemanticInfo recalculateSemanticInfo(); // TODO: Remove me

    // The last one the processor worked out, kept here rather than only on
    // CppEditorWidget: it belongs to the file, not to a view of it, and it is
    // what a quick fix needs. The widget still keeps its own copy, because it
    // patches the local uses into it as the caret moves.
    SemanticInfo semanticInfo() const;
    // Whether it still describes what the document says. Local uses are not
    // part of the question here, for the same reason.
    bool isSemanticInfoValid() const;

    bool handleKeyPress(QKeyEvent *event, const QTextCursor &cursor) override;

    std::unique_ptr<TextEditor::AssistInterface> createAssistInterface(
        const QTextCursor &cursor,
        TextEditor::AssistKind kind,
        TextEditor::AssistReason reason,
        Core::IEditor *editor = nullptr) const override;

    void setPreferredParseContext(const QString &parseContextId);
    void updateSoftPreferredParseContext(const ProjectExplorer::Node *currentNode);
    void setExtraPreprocessorDirectives(const QByteArray &directives);
    // Ask which extra directives this file should be parsed with. A dialog,
    // but not a view's: what it changes is the document.
    void showPreProcessorDialog();

    QList<QAction *> ownToolBarActions() const override;

    // Diagnostics do not underline a line's leading whitespace: an error on a
    // deeply indented line would otherwise draw a long squiggle in front of
    // the token it is about. Static, being a transformation on the ranges.
    static const QList<QTextEdit::ExtraSelection>
    unselectLeadingWhitespace(const QList<QTextEdit::ExtraSelection> &selections);
    TextEditor::ToolBarChoice *toolBarChoice() const override;

    // the blocks list must be sorted
    void setIfdefedOutBlocks(const QList<TextEditor::BlockRange> &blocks);

    void scheduleProcessDocument();

    Internal::ParseContextModel &parseContextModel();
    Internal::OutlineModel &outlineModel();
    void updateOutline();

    QFuture<CursorInfo> cursorInfo(const CursorInfoParams &params);
    TextEditor::TabSettingsData tabSettings() const override;

    bool usesClangd() const;

#ifdef WITH_TESTS
    QList<TextEditor::BlockRange> ifdefedOutBlocks() const;
#endif

signals:
    void codeWarningsUpdated(unsigned contentsRevision,
                             const QList<QTextEdit::ExtraSelection> selections,
                             const QList<TextEditor::RefactorMarker> &refactorMarkers);

    void cppDocumentUpdated(const CPlusPlus::Document::Ptr document);    // TODO: Remove me
    void semanticInfoUpdated(const SemanticInfo semanticInfo); // TODO: Remove me

    void preprocessorSettingsChanged(bool customSettings);

#ifdef WITH_TESTS
    void ifdefedOutBlocksApplied();
#endif

protected:
    void applyFontSettings() override;
    Utils::Result<> saveImpl(const Utils::FilePath &filePath, SaveOption option) override;
    void slotCodeStyleSettingsChanged() override;
    void removeTrailingWhitespace(const QTextBlock &block) override;

private:
    void processDocument();

    class Private;
    Private * const d;
};

#ifdef WITH_TESTS
namespace Internal { QObject *createCodeWarningsTest(); }
#endif

} // namespace CppEditor

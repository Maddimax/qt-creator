// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"

#include <cplusplus/CppDocument.h>

#include <texteditor/codeassist/assistenums.h>
#include <texteditor/texteditor.h>

#include <QScopedPointer>

#include <functional>

namespace TextEditor {
class BlockRange;
class IAssistProposal;
class IAssistProvider;
}

namespace CppEditor {
class CppEditorDocument;
class ProjectPart;
class SemanticInfo;

namespace Internal {
class CppDeclDefLinkController;
class CppEditorOutline;
class CppEditorWidgetPrivate;
class FunctionDeclDefLink;
} // namespace Internal

namespace Internal {
// What Enter does inside a comment. Lives here because its helpers do; the
// document is what calls it, so that either view gets the same answer.
bool trySplitComment(TextEditor::TextDocument *document,
                     QTextCursor cursor,
                     const CPlusPlus::Snapshot &snapshot);

// What Enter does inside a string literal, which is end it and start another
// on the next line. Here for the same reason as trySplitComment(), and called
// from the same place.
bool trySplitString(TextEditor::TextDocument *document, QKeyEvent *event, QTextCursor cursor);
} // namespace Internal

class CPPEDITOR_EXPORT CppEditorWidget : public TextEditor::TextEditorWidget
{
    Q_OBJECT

public:
    CppEditorWidget();
    ~CppEditorWidget() override;

    static const QList<CppEditorWidget *> editorWidgetsForDocument(TextEditor::TextDocument *doc);

    CppEditorDocument *cppEditorDocument() const;

    bool isSemanticInfoValidExceptLocalUses() const;
    bool isSemanticInfoValid() const;
    bool isRenaming() const;

    std::shared_ptr<Internal::FunctionDeclDefLink> declDefLink() const;
    Internal::CppDeclDefLinkController *declDefLinkController() const;
    void applyDeclDefLinkChanges(bool jumpToMatch);

    std::unique_ptr<TextEditor::AssistInterface> createAssistInterface(
            TextEditor::AssistKind kind,
            TextEditor::AssistReason reason) const override;

    void encourageApply() override;

    void paste() override;
    void cut() override;
    void selectAll() override;

    void switchDeclarationDefinition(bool inNextSplit);
    void goToParentImpl(bool inNextSplit);
    void showPreProcessorWidget();

    void findUsages() override;
    void findUsages(QTextCursor cursor);
    void renameUsages(const QString &replacement = QString(),
                      QTextCursor cursor = QTextCursor());
    void renameUsages(const Utils::FilePath &filePath,
                      const QString &replacement = QString(),
                      QTextCursor cursor = QTextCursor(),
                      const std::function<void()> &callback = {});
    void renameSymbolUnderCursor() override;
    void inInlineRename(bool *active) override;

    bool selectBlockUp() override;
    bool selectBlockDown() override;

    static void updateWidgetHighlighting(QWidget *widget, bool highlight);
    static bool isWidgetHighlighted(QWidget *widget);

    SemanticInfo semanticInfo() const;
    void updateSemanticInfo();
    void invokeTextEditorWidgetAssist(TextEditor::AssistKind assistKind,
                                      TextEditor::IAssistProvider *provider);



protected:
    bool event(QEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool handleStringSplitting(QKeyEvent *e) const;

    void findLinkAt(const QTextCursor &cursor,
                    const Utils::LinkHandler &processLinkCallback,
                    bool resolveTarget = true,
                    bool inNextSplit = false) override;

    void findTypeAt(const QTextCursor &cursor,
                    const Utils::LinkHandler &processLinkCallback,
                    bool resolveTarget = true,
                    bool inNextSplit = false) override;

private:
    void updateFunctionDeclDefLink();
    void updateFunctionDeclDefLinkNow();
    void abortDeclDefLink();


    void updateSemanticInfo(const SemanticInfo &semanticInfo,
                            bool updateUseSelectionSynchronously = false);
    void updatePreprocessorButtonTooltip();

    void processKeyNormally(QKeyEvent *e);

    void finalizeInitialization() override;
    void finalizeInitializationAfterDuplication(TextEditorWidget *other) override;

    unsigned documentRevision() const;
    bool isOldStyleSignalOrSlot() const;
    bool followUrl(const QTextCursor &cursor, const Utils::LinkHandler &processLinkCallback);

    QMenu *createRefactorMenu(QWidget *parent) const;

    const ProjectPart *projectPart() const;

    void handleOutlineChanged(const QWidget* newOutline);
    void addRefactoringActions(QMenu *menu) const;

private:
    QScopedPointer<Internal::CppEditorWidgetPrivate> d;
};

} // namespace CppEditor

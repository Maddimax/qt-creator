// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef CPPLOCALRENAMING
#define CPPLOCALRENAMING

#include <texteditor/texteditor.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditorconstants.h>

#include <QPointer>

namespace Core { class IEditor; }
namespace Utils { class Link; using Links = QList<Link>; }
namespace TextEditor { class TextEditorWidget; }

namespace CppEditor::Internal {

class CppUseSelectionsUpdater;

#ifdef WITH_TESTS
QObject *createLocalRenamingTest();
#endif

class CppLocalRenaming : public TextEditor::EditHandler
{
    Q_OBJECT
    Q_DISABLE_COPY(CppLocalRenaming)

public:
    using Selections = QList<TextEditor::TextDocument::ExtraSelection>;

    // Renaming in a view that is not a widget. Parented to the editor, which
    // is where the view looks for it.
    explicit CppLocalRenaming(Core::IEditor *editor);
    // Renaming a widget owns itself. The widget is built before the editor
    // that wraps it, so the editor is looked up when it is first needed.
    explicit CppLocalRenaming(TextEditor::TextEditorWidget *editorWidget);

    bool start();
    bool isActive() const;
    void stop();

    // Delegates for the view
    bool handlePaste() override;
    bool handleCut() override;
    bool handleSelectAll() override;
    bool handleRename() override;

    // E.g. limit navigation keys to selection, stop() on Esc/Return or leave
    // the key to the view
    bool handleKeyPress(QKeyEvent *e, const std::function<void()> &processNormally) override;

    bool encourageApply() override;
    void onContentsChangeOfEditorWidgetDocument(int position, int charsRemoved, int charsAdded);

    void updateSelectionsForVariableUnderCursor(const Selections &selections);
    // Where the uses this renames come from. Set where the view asks this to
    // take a rename on its own: what the name is used for has to be current
    // before the answer is any good.
    void setUseSelectionsUpdater(CppUseSelectionsUpdater *updater);
    bool isSameSelection(int cursorPosition) const;

signals:
    void started();
    void finished();

private:
    CppLocalRenaming();

    Core::IEditor *editor() const;
    TextEditor::TextDocument *textDocument() const;
    Selections selectionsForUses(const Utils::Links &uses, uint nameLength) const;

    // The "rename selection" is the local use selection on which the user started the renaming
    bool findRenameSelection(int cursorPosition);
    void forgetRenamingSelection();
    static bool isWithinSelection(const TextEditor::TextDocument::ExtraSelection &selection,
                                  int position);
    bool isWithinRenameSelection(int position);

    TextEditor::TextDocument::ExtraSelection &renameSelection();
    int renameSelectionBegin() { return renameSelection().cursor.selectionStart(); }
    int renameSelectionEnd() { return renameSelection().cursor.selectionEnd(); }

    void updateRenamingSelectionCursor(const QTextCursor &cursor);
    void updateRenamingSelectionFormat(const QTextCharFormat &format);

    void changeOtherSelectionsText(const QString &text);

    void startRenameChange();
    void finishRenameChange();

    void updateViewWithSelections();

    QTextCharFormat textCharFormat(TextEditor::TextStyle category) const;

private:
    mutable QPointer<Core::IEditor> m_editor;
    QPointer<CppUseSelectionsUpdater> m_useSelectionsUpdater;
    QPointer<TextEditor::TextEditorWidget> m_editorWidget;

    Selections m_selections;
    int m_renameSelectionIndex;
    bool m_modifyingSelections;
    bool m_renameSelectionChanged;
    bool m_firstRenameChangeExpected;
};

} // namespace CppEditor::Internal

#endif // CPPLOCALRENAMING

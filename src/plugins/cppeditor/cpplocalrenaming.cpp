// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpplocalrenaming.h"

#include "cppeditordocument.h"
#include "cppmodelmanager.h"
#include "cpptoolsreuse.h"
#include "cursorineditor.h"
#include "cppuseselectionsupdater.h"

#include <coreplugin/editormanager/ieditor.h>

#include <texteditor/fontsettings.h>
#include <texteditor/texteditor.h>
#include <texteditor/textdocument.h>

#include <utils/link.h>
#include <utils/qtcassert.h>
#include <utils/textutils.h>

/*!
    \class CppEditor::Internal::CppLocalRenaming
    \brief Renaming every use of a local name at once, in place.

    \internal

    Local use selections must be first set/updated with updateLocalUseSelections().
    Afterwards the local renaming can be started with start(). The view can
    then delegate work related to the local renaming mode to the handle*
    functions - a widget by calling them, a Qt Quick view by finding this as
    the TextEditor::EditHandler its editor carries.
 */

namespace {

void modifyCursorSelection(QTextCursor &cursor, int position, int anchor)
{
    cursor.setPosition(anchor);
    cursor.setPosition(position, QTextCursor::KeepAnchor);
}

} // anonymous namespace

namespace CppEditor::Internal {

CppLocalRenaming::CppLocalRenaming(Core::IEditor *editor)
    : TextEditor::EditHandler(editor)
    , m_editor(editor)
    , m_modifyingSelections(false)
    , m_renameSelectionChanged(false)
    , m_firstRenameChangeExpected(false)
{
    forgetRenamingSelection();
}

CppLocalRenaming::CppLocalRenaming(TextEditor::TextEditorWidget *editorWidget)
    : m_editorWidget(editorWidget)
    , m_modifyingSelections(false)
    , m_renameSelectionChanged(false)
    , m_firstRenameChangeExpected(false)
{
    forgetRenamingSelection();
}

Core::IEditor *CppLocalRenaming::editor() const
{
    if (!m_editor)
        m_editor = editorFor(m_editorWidget);
    return m_editor;
}

TextEditor::TextDocument *CppLocalRenaming::textDocument() const
{
    Core::IEditor * const forView = editor();
    return forView ? qobject_cast<TextEditor::TextDocument *>(forView->document()) : nullptr;
}

void CppLocalRenaming::updateSelectionsForVariableUnderCursor(
        const Selections &selections)
{
    if (isActive())
        return;

    m_selections = selections;
}

bool CppLocalRenaming::start()
{
    stop();

    if (findRenameSelection(TextEditor::textCursorOf(editor()).position())) {
        updateRenamingSelectionFormat(textCharFormat(TextEditor::C_OCCURRENCES_RENAME));
        m_firstRenameChangeExpected = true;
        updateViewWithSelections();
        emit started();
        return true;
    }

    return false;
}

bool CppLocalRenaming::handlePaste()
{
    if (!isActive())
        return false;

    startRenameChange();
    TextEditor::pasteIn(editor());
    finishRenameChange();
    return true;
}

bool CppLocalRenaming::handleCut()
{
    if (!isActive())
        return false;

    startRenameChange();
    TextEditor::cutIn(editor());
    finishRenameChange();
    return true;
}

bool CppLocalRenaming::handleSelectAll()
{
    if (!isActive())
        return false;

    QTextCursor cursor = TextEditor::textCursorOf(editor());
    if (!isWithinRenameSelection(cursor.position()))
        return false;

    modifyCursorSelection(cursor, renameSelectionBegin(), renameSelectionEnd());
    TextEditor::setTextCursorOf(editor(), cursor);
    return true;
}

void CppLocalRenaming::setUseSelectionsUpdater(CppUseSelectionsUpdater *updater)
{
    m_useSelectionsUpdater = updater;
}

bool CppLocalRenaming::handleRename()
{
    Core::IEditor * const forView = editor();
    TextEditor::TextDocument * const document = textDocument();
    if (!forView || !document)
        return false;

    const QTextCursor cursor = TextEditor::textCursorOf(forView);
    if (cursor.isNull())
        return false;
    // Already renaming that very name, which is no reason to start again.
    if (isActive() && isSameSelection(cursor.position()))
        return true;

    if (m_useSelectionsUpdater) {
        m_useSelectionsUpdater->abortSchedule();
        // The built-in model answers with no uses of its own, so what is drawn
        // for the name right now is all there is to rename - and it has to be
        // current. clangd finds them itself, below, and needs no such refresh.
        if (!CppModelManager::usesClangd(document)) {
            if (auto * const cppDocument = qobject_cast<CppEditorDocument *>(document))
                cppDocument->recalculateSemanticInfo();
            m_useSelectionsUpdater->update(CppUseSelectionsUpdater::CallType::Synchronous);
        }
    }

    const QPointer<CppLocalRenaming> alive(this);
    auto renameSymbols = [alive, cursor](const QString &symbolName, const Utils::Links &uses,
                                         int revision) {
        if (!alive)
            return;
        Core::IEditor * const forView = alive->editor();
        TextEditor::TextDocument * const document = alive->textDocument();
        if (!forView || !document || revision != document->document()->revision())
            return;
        if (!uses.isEmpty()) {
            const Selections selections
                = alive->selectionsForUses(uses, static_cast<uint>(symbolName.size()));
            TextEditor::setViewSelections(forView,
                                          TextEditor::TextEditorWidget::CodeSemanticsSelection,
                                          selections);
            alive->stop();
            alive->updateSelectionsForVariableUnderCursor(selections);
        }
        // A name used somewhere this view cannot show is not renamed here at
        // all, and goes to a search across the project instead.
        if (!alive->start())
            CppEditor::renameUsagesOf(forView, {}, cursor);
    };

    CppModelManager::startLocalRenaming(CursorInEditor{cursor, document->filePath(), nullptr,
                                                       document},
                                        nullptr,
                                        std::move(renameSymbols));
    return true;
}

CppLocalRenaming::Selections CppLocalRenaming::selectionsForUses(const Utils::Links &uses,
                                                                uint nameLength) const
{
    TextEditor::TextDocument * const document = textDocument();
    QTC_ASSERT(document, return {});
    const QTextCharFormat format
        = document->fontSettings().toTextCharFormat(TextEditor::C_OCCURRENCES);

    Selections selections;
    selections.reserve(uses.size());
    for (const Utils::Link &use : uses) {
        selections.append({Utils::Text::selectAt(QTextCursor(document->document()),
                                                 use.target.line,
                                                 use.target.column,
                                                 nameLength),
                           format});
    }
    return selections;
}

bool CppLocalRenaming::isActive() const
{
    return m_renameSelectionIndex != -1;
}

bool CppLocalRenaming::handleKeyPress(QKeyEvent *e, const std::function<void()> &processNormally)
{
    if (!isActive())
        return false;

    QTextCursor cursor = TextEditor::textCursorOf(editor());
    const int cursorPosition = cursor.position();
    const QTextCursor::MoveMode moveMode = (e->modifiers() & Qt::ShiftModifier)
            ? QTextCursor::KeepAnchor
            : QTextCursor::MoveAnchor;

    switch (e->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
    case Qt::Key_Escape:
        stop();
        e->accept();
        return true;
    case Qt::Key_Home: {
        // Send home to start of name when within the name and not at the start
        if (renameSelectionBegin() < cursorPosition  && cursorPosition <= renameSelectionEnd()) {
            cursor.setPosition(renameSelectionBegin(), moveMode);
            TextEditor::setTextCursorOf(editor(), cursor);
            e->accept();
            return true;
        }
        break;
    }
    case Qt::Key_End: {
        // Send end to end of name when within the name and not at the end
        if (renameSelectionBegin() <= cursorPosition && cursorPosition < renameSelectionEnd()) {
            cursor.setPosition(renameSelectionEnd(), moveMode);
            TextEditor::setTextCursorOf(editor(), cursor);
            e->accept();
            return true;
        }
        break;
    }
    case Qt::Key_Backspace: {
        if (cursorPosition == renameSelectionBegin() && !cursor.hasSelection()) {
            // Eat backspace at start of name when there is no selection
            e->accept();
            return true;
        }
        break;
    }
    case Qt::Key_Delete: {
        if (cursorPosition == renameSelectionEnd() && !cursor.hasSelection()) {
            // Eat delete at end of name when there is no selection
            e->accept();
            return true;
        }
        break;
    }
    default: {
        break;
    }
    } // switch

    startRenameChange();

    const bool wantEditBlock = isWithinRenameSelection(cursorPosition);
    const int undoSizeBeforeEdit = textDocument()->document()->availableUndoSteps();
    if (wantEditBlock) {
        if (m_firstRenameChangeExpected) // Change inside rename selection
            cursor.beginEditBlock();
        else
            cursor.joinPreviousEditBlock();
    }
    processNormally();
    if (wantEditBlock) {
        cursor.endEditBlock();
        if (m_firstRenameChangeExpected
                // QTCREATORBUG-16350
                && textDocument()->document()->availableUndoSteps() != undoSizeBeforeEdit) {
            m_firstRenameChangeExpected = false;
        }
    }
    finishRenameChange();
    return true;
}

bool CppLocalRenaming::encourageApply()
{
    if (!isActive())
        return false;
    finishRenameChange();
    return true;
}

TextEditor::TextDocument::ExtraSelection &CppLocalRenaming::renameSelection()
{
    return m_selections[m_renameSelectionIndex];
}

void CppLocalRenaming::updateRenamingSelectionCursor(const QTextCursor &cursor)
{
    QTC_ASSERT(isActive(), return);
    renameSelection().cursor = cursor;
}

void CppLocalRenaming::updateRenamingSelectionFormat(const QTextCharFormat &format)
{
    QTC_ASSERT(isActive(), return);
    renameSelection().format = format;
}

void CppLocalRenaming::forgetRenamingSelection()
{
    m_renameSelectionIndex = -1;
}

bool CppLocalRenaming::isWithinSelection(const TextEditor::TextDocument::ExtraSelection &selection, int position)
{
    return selection.cursor.selectionStart() <= position
           && position <= selection.cursor.selectionEnd();
}

bool CppLocalRenaming::isWithinRenameSelection(int position)
{
    return isWithinSelection(renameSelection(), position);
}

bool CppLocalRenaming::isSameSelection(int cursorPosition) const
{
    if (!isActive())
        return false;

    const TextEditor::TextDocument::ExtraSelection &sel = m_selections[m_renameSelectionIndex];
    return isWithinSelection(sel, cursorPosition);
}

bool CppLocalRenaming::findRenameSelection(int cursorPosition)
{
    for (int i = 0, total = m_selections.size(); i < total; ++i) {
        const TextEditor::TextDocument::ExtraSelection &sel = m_selections.at(i);
        if (isWithinSelection(sel, cursorPosition)) {
            m_renameSelectionIndex = i;
            return true;
        }
    }

    return false;
}

void CppLocalRenaming::changeOtherSelectionsText(const QString &text)
{
    for (int i = 0, total = m_selections.size(); i < total; ++i) {
        if (i == m_renameSelectionIndex)
            continue;

        TextEditor::TextDocument::ExtraSelection &selection = m_selections[i];
        const int pos = selection.cursor.selectionStart();
        selection.cursor.removeSelectedText();
        selection.cursor.insertText(text);
        selection.cursor.setPosition(pos, QTextCursor::KeepAnchor);
    }
}

void CppLocalRenaming::onContentsChangeOfEditorWidgetDocument(int position,
                                                              int charsRemoved,
                                                              int charsAdded)
{
    Q_UNUSED(charsRemoved)

    if (!isActive() || m_modifyingSelections)
        return;

    if (position + charsAdded == renameSelectionBegin()) // Insert at beginning, expand cursor
        modifyCursorSelection(renameSelection().cursor, position, renameSelectionEnd());

    // Keep in mind that cursor position and anchor move automatically
    m_renameSelectionChanged = isWithinRenameSelection(position)
            && isWithinRenameSelection(position + charsAdded);

    if (!m_renameSelectionChanged)
        stop();
}

void CppLocalRenaming::startRenameChange()
{
    m_renameSelectionChanged = false;
}

void CppLocalRenaming::updateViewWithSelections()
{
    TextEditor::setViewSelections(editor(),
                                  TextEditor::TextEditorWidget::CodeSemanticsSelection,
                                  m_selections);
}

QTextCharFormat CppLocalRenaming::textCharFormat(TextEditor::TextStyle category) const
{
    TextEditor::TextDocument * const document = textDocument();
    QTC_ASSERT(document, return {});
    return document->fontSettings().toTextCharFormat(category);
}

void CppLocalRenaming::finishRenameChange()
{
    if (!m_renameSelectionChanged)
        return;

    m_modifyingSelections = true;

    QTextCursor cursor = TextEditor::textCursorOf(editor());
    cursor.joinPreviousEditBlock();

    modifyCursorSelection(cursor, renameSelectionBegin(), renameSelectionEnd());
    updateRenamingSelectionCursor(cursor);
    changeOtherSelectionsText(cursor.selectedText());
    updateViewWithSelections();

    cursor.endEditBlock();

    m_modifyingSelections = false;
}

void CppLocalRenaming::stop()
{
    if (!isActive())
        return;

    updateRenamingSelectionFormat(textCharFormat(TextEditor::C_OCCURRENCES));
    updateViewWithSelections();
    forgetRenamingSelection();

    emit finished();
}

} // namespace CppEditor::Internal

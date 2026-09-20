// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/textutils.h>
#include <utils/treemodel.h>

#include <QTimer>

namespace Core {
class IEditor;
}

namespace TextEditor {
class TextDocument;
class TextEditorWidget;
class ToolBarOutline;
} // namespace TextEditor

namespace CMakeProjectManager::Internal {

class OutlineItem;

// The project, functions, macros, targets and variables a CMake file names, in
// source order and nested the way the file nests them. Follows the text of the
// document.
class CMakeOutlineModel final : public Utils::TreeModel<>
{
public:
    explicit CMakeOutlineModel(TextEditor::TextDocument *document);

    enum Match {
        MatchAll,
        MatchWithoutVariables
    };

    // Where the name the item stands for is written. Invalid for an index that
    // stands for no item.
    Utils::Text::Position positionFromIndex(const QModelIndex &index) const;

    bool isVariable(const QModelIndex &index) const;

    // The innermost item the position sits in, or the last one written above
    // it.
    QModelIndex indexForPosition(const Utils::Text::Position &position,
                                 Match match = MatchAll,
                                 const QModelIndex &parent = {}) const;

private:
    const OutlineItem *itemAt(const QModelIndex &index) const;
    void rebuild();

    TextEditor::TextDocument * const m_document;
    QTimer m_updateTimer;
};

// The outline combo box of the widget editor's tool bar.
QWidget *createCMakeOutlineComboBox(TextEditor::TextEditorWidget *editorWidget,
                                    CMakeOutlineModel *model);

// The same for a view that draws its own tool bar: parented to \a editor,
// where the view finds it, and following its caret.
TextEditor::ToolBarOutline *createCMakeToolBarOutline(Core::IEditor *editor,
                                                      CMakeOutlineModel *model);

void setupCMakeOutline();

#ifdef WITH_TESTS
QObject *createCMakeOutlineTest();
#endif

} // CMakeProjectManager::Internal

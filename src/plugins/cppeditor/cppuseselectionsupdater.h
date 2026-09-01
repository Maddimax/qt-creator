// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppcursorinfo.h"
#include "cppsemanticinfo.h"

#include <texteditor/textdocument.h>

#include <QFutureWatcher>
#include <QPointer>
#include <QTextEdit>
#include <QTimer>

namespace Core { class IEditor; }

namespace CppEditor {
class CppEditorDocument;
class CppEditorWidget;

namespace Internal {

class CppUseSelectionsUpdater : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(CppUseSelectionsUpdater)

public:
    explicit CppUseSelectionsUpdater(CppEditorWidget *editorWidget);
    // For a view that is not that widget. The same work, asked of the editor
    // and of the document rather than of a QPlainTextEdit.
    explicit CppUseSelectionsUpdater(Core::IEditor *editor);
    ~CppUseSelectionsUpdater() override;

    void scheduleUpdate();
    void abortSchedule();

    enum class CallType { Synchronous, Asynchronous };
    enum class RunnerInfo { AlreadyUpToDate, Started, FailedToStart, Invalid }; // For async case.
    RunnerInfo update(CallType callType = CallType::Asynchronous);

signals:
    void finished(SemanticInfo::LocalUseMap localUses, bool success);
    void selectionsForVariableUnderCursorUpdated(const QList<QTextEdit::ExtraSelection> &);

private:
    CppUseSelectionsUpdater();
    bool isSameIdentifierAsBefore(const QTextCursor &cursorAtWordStart) const;
    void processResults(const CursorInfo &result);
    void onFindUsesFinished();

    // Convenience
    using ExtraSelections = QList<TextEditor::TextDocument::ExtraSelection>;
    ExtraSelections toExtraSelections(const CursorInfo::Ranges &ranges,
                                      TextEditor::TextStyle style);
    ExtraSelections currentUseSelections() const;
    ExtraSelections updateUseSelections(const CursorInfo::Ranges &selections);
    void updateUnusedSelections(const CursorInfo::Ranges &selections);

    // What the view is asked for, whichever view it is. The editor is found
    // rather than held for the widget: the widget is built before the editor
    // that shows it, and this is made with the widget.
    Core::IEditor *editor() const;
    CppEditorDocument *cppDocument() const;
    QTextCursor cursor() const;
    int revision() const;
    // Only the widget has an editing mode to be in the middle of.
    bool isRenaming() const;
    SemanticInfo semanticInfo() const;

private:
    CppEditorWidget * const m_editorWidget = nullptr;
    const QPointer<Core::IEditor> m_editor;

    QTimer m_timer;

    std::unique_ptr<QFutureWatcher<CursorInfo>> m_runnerWatcher;
    int m_runnerRevision = -1;
    int m_runnerWordStartPosition = -1;
    bool m_updateSelections = true;
};

#ifdef WITH_TESTS
QObject *createUseSelectionsTest();
#endif

} // namespace Internal
} // namespace CppEditor

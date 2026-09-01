// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppuseselectionsupdater.h"

#include "cppeditordocument.h"
#include "cppeditorwidget.h"
#include "cppmodelmanager.h"
#include "cpptoolsreuse.h"

#include <coreplugin/editormanager/ieditor.h>

#include <texteditor/fontsettings.h>
#include <texteditor/texteditor.h>

#include <utils/futuresynchronizer.h>
#include <utils/qtcassert.h>
#include <utils/textutils.h>

#ifdef WITH_TESTS
#include "clangdsettings.h"
#include <coreplugin/editormanager/editormanager.h>
#include <utils/temporarydirectory.h>
#include <QScopeGuard>
#include <QTest>
#endif

#include <QCoreApplication>
#include <QTextBlock>
#include <QTextCursor>

enum { updateUseSelectionsInternalInMs = 500 };

namespace CppEditor::Internal {

CppUseSelectionsUpdater::CppUseSelectionsUpdater(CppEditorWidget *editorWidget)
    : m_editorWidget(editorWidget)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(updateUseSelectionsInternalInMs);
    connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
}

CppUseSelectionsUpdater::CppUseSelectionsUpdater(Core::IEditor *editor)
    : m_editor(editor)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(updateUseSelectionsInternalInMs);
    connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
}

Core::IEditor *CppUseSelectionsUpdater::editor() const
{
    return m_editorWidget ? editorFor(m_editorWidget) : m_editor.data();
}

CppEditorDocument *CppUseSelectionsUpdater::cppDocument() const
{
    if (m_editorWidget)
        return qobject_cast<CppEditorDocument *>(m_editorWidget->textDocument());
    return m_editor ? qobject_cast<CppEditorDocument *>(m_editor->document()) : nullptr;
}

QTextCursor CppUseSelectionsUpdater::cursor() const
{
    return m_editorWidget ? m_editorWidget->textCursor() : TextEditor::textCursorOf(m_editor);
}

int CppUseSelectionsUpdater::revision() const
{
    CppEditorDocument * const document = cppDocument();
    return document ? document->document()->revision() : -1;
}

bool CppUseSelectionsUpdater::isRenaming() const
{
    return m_editorWidget && m_editorWidget->isRenaming();
}

// The widget's own where there is one: it has the local uses patched into it
// as the caret moves, and that is what this is about to work out again.
SemanticInfo CppUseSelectionsUpdater::semanticInfo() const
{
    if (m_editorWidget)
        return m_editorWidget->semanticInfo();
    CppEditorDocument * const document = cppDocument();
    return document ? document->semanticInfo() : SemanticInfo();
}

CppUseSelectionsUpdater::~CppUseSelectionsUpdater()
{
    if (m_runnerWatcher)
        m_runnerWatcher->cancel();
}

void CppUseSelectionsUpdater::scheduleUpdate()
{
    m_timer.start();
}

void CppUseSelectionsUpdater::abortSchedule()
{
    m_timer.stop();
}

CppUseSelectionsUpdater::RunnerInfo CppUseSelectionsUpdater::update(CallType callType)
{
    CppEditorDocument * const cppEditorDocument = cppDocument();
    QTC_ASSERT(cppEditorDocument, return RunnerInfo::FailedToStart);

    const QTextCursor caret = cursor();
    if (caret.isNull())
        return RunnerInfo::FailedToStart;

    m_updateSelections = !CppModelManager::usesClangd(cppEditorDocument) && !isRenaming();

    CursorInfoParams params;
    params.semanticInfo = semanticInfo();
    params.textCursor = Utils::Text::wordStartCursor(caret);

    if (callType == CallType::Asynchronous) {
        if (isSameIdentifierAsBefore(params.textCursor))
            return RunnerInfo::AlreadyUpToDate;

        if (m_runnerWatcher)
            m_runnerWatcher->cancel();

        m_runnerWatcher.reset(new QFutureWatcher<CursorInfo>);
        connect(m_runnerWatcher.get(), &QFutureWatcherBase::finished,
                this, &CppUseSelectionsUpdater::onFindUsesFinished);

        m_runnerRevision = revision();
        m_runnerWordStartPosition = params.textCursor.position();

        m_runnerWatcher->setFuture(cppEditorDocument->cursorInfo(params));
        Utils::futureSynchronizer()->addFuture(m_runnerWatcher->future());
        return RunnerInfo::Started;
    } else { // synchronous case
        abortSchedule();

        const int startRevision = cppEditorDocument->document()->revision();
        QFuture<CursorInfo> future = cppEditorDocument->cursorInfo(params);
        if (future.isCanceled())
            return RunnerInfo::Invalid;

        // QFuture::waitForFinished seems to block completely, not even
        // allowing to process events from QLocalSocket.
        while (!future.isFinished()) {
            if (future.isCanceled())
                return RunnerInfo::Invalid;

            QTC_ASSERT(startRevision == cppEditorDocument->document()->revision(),
                       return RunnerInfo::Invalid);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        processResults(future.result());
        return RunnerInfo::Invalid;
    }
}

bool CppUseSelectionsUpdater::isSameIdentifierAsBefore(const QTextCursor &cursorAtWordStart) const
{
    return m_runnerRevision != -1
        && m_runnerRevision == revision()
        && m_runnerWordStartPosition == cursorAtWordStart.position();
}

void CppUseSelectionsUpdater::processResults(const CursorInfo &result)
{
    if (m_updateSelections) {
        ExtraSelections localVariableSelections;
        if (!result.useRanges.isEmpty() || !currentUseSelections().isEmpty()) {
            ExtraSelections selections = updateUseSelections(result.useRanges);
            if (result.areUseRangesForLocalVariable)
                localVariableSelections = selections;
        }
        updateUnusedSelections(result.unusedVariablesRanges);
        // The local renaming this feeds is the widget's, and so is its type.
        QList<QTextEdit::ExtraSelection> forRenaming;
        forRenaming.reserve(localVariableSelections.size());
        for (const TextEditor::TextDocument::ExtraSelection &selection : localVariableSelections)
            forRenaming.append({selection.cursor, selection.format});
        emit selectionsForVariableUnderCursorUpdated(forRenaming);
    }
    emit finished(result.localUses, true);
}

void CppUseSelectionsUpdater::onFindUsesFinished()
{
    QTC_ASSERT(m_runnerWatcher,
               emit finished(SemanticInfo::LocalUseMap(), false); return);

    if (m_runnerWatcher->isCanceled()) {
        emit finished(SemanticInfo::LocalUseMap(), false);
        return;
    }
    if (m_runnerRevision != revision()) {
        emit finished(SemanticInfo::LocalUseMap(), false);
        return;
    }
    if (m_runnerWordStartPosition != Utils::Text::wordStartCursor(cursor()).position()) {
        emit finished(SemanticInfo::LocalUseMap(), false);
        return;
    }

    processResults(m_runnerWatcher->result());

    m_runnerWatcher.release()->deleteLater();
}

CppUseSelectionsUpdater::ExtraSelections
CppUseSelectionsUpdater::toExtraSelections(const CursorInfo::Ranges &ranges,
                                           TextEditor::TextStyle style)
{
    CppUseSelectionsUpdater::ExtraSelections selections;
    selections.reserve(ranges.size());

    CppEditorDocument * const cppEditorDocument = cppDocument();
    QTC_ASSERT(cppEditorDocument, return selections);
    QTextDocument * const document = cppEditorDocument->document();
    const QTextCharFormat format = cppEditorDocument->fontSettings().toTextCharFormat(style);

    for (const CursorInfo::Range &range : ranges) {
        const int position
                = document->findBlockByNumber(static_cast<int>(range.line) - 1).position()
                    + static_cast<int>(range.column) - 1;
        const int anchor = position + static_cast<int>(range.length);

        QTextCursor selection(document);
        selection.setPosition(anchor);
        selection.setPosition(position, QTextCursor::KeepAnchor);

        selections.append({selection, format});
    }

    return selections;
}

CppUseSelectionsUpdater::ExtraSelections
CppUseSelectionsUpdater::currentUseSelections() const
{
    return TextEditor::viewSelections(editor(),
                                     TextEditor::TextEditorWidget::CodeSemanticsSelection);
}

CppUseSelectionsUpdater::ExtraSelections
CppUseSelectionsUpdater::updateUseSelections(const CursorInfo::Ranges &ranges)
{
    const ExtraSelections selections = toExtraSelections(ranges, TextEditor::C_OCCURRENCES);
    TextEditor::setViewSelections(editor(),
                                  TextEditor::TextEditorWidget::CodeSemanticsSelection,
                                  selections);

    return selections;
}

void CppUseSelectionsUpdater::updateUnusedSelections(const CursorInfo::Ranges &ranges)
{
    const ExtraSelections selections = toExtraSelections(ranges, TextEditor::C_OCCURRENCES_UNUSED);
    TextEditor::setViewSelections(editor(),
                                  TextEditor::TextEditorWidget::UnusedSymbolSelection,
                                  selections);
}


#ifdef WITH_TESTS

// The symbol under the caret, marked everywhere else it is used. Every step of
// it went through CppEditorWidget - the caret, the semantic info, and the
// extra selections it drew - so a C++ file in the Qt Quick editor showed none
// of it, and nobody even made an updater for that editor.
class UseSelectionsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testUsesAreMarkedInAnyView()
    {
        Utils::TemporaryDirectory dir("cpp-use-selections-without-a-widget");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("main.cpp");
        QVERIFY(file.writeFileContents(
            "int main()\n{\n    int alpha = 1;\n    return alpha + alpha;\n}\n"));

        // The built-in model works these out; clangd marks them itself, and
        // usesClangd() is settled when the document is opened.
        const bool wasClangd = ClangdSettings::instance().useClangd();
        const QScopeGuard restoreClangd(
            [wasClangd] { ClangdSettings::setUseClangd(wasClangd); });
        ClangdSettings::setUseClangd(false);

        TextEditor::TextEditorFactory * const editorFactory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(editorFactory, "no editor factory claims a C++ file");
        const bool wasQuick = editorFactory->usesQuickEditor();
        const QScopeGuard restore(
            [editorFactory, wasQuick] { editorFactory->setUsesQuickEditor(wasQuick); });
        editorFactory->setUsesQuickEditor(true);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");

        auto * const document = qobject_cast<CppEditorDocument *>(editor->document());
        QVERIFY(document);
        document->recalculateSemanticInfo();
        QTRY_VERIFY2(document->isSemanticInfoValid(),
                     "the document never worked out what the file says");

        // Made by the plugin for this editor, because nothing else would.
        auto * const updater = editor->findChild<CppUseSelectionsUpdater *>();
        QVERIFY2(updater, "nobody made an updater for an editor that is not a widget");

        // On alpha where it is declared. All three of it are one symbol, so
        // all three are marked.
        editor->gotoLine(3, 10);
        updater->update(CppUseSelectionsUpdater::CallType::Synchronous);

        const QList<TextEditor::TextDocument::ExtraSelection> marked
            = TextEditor::viewSelections(editor,
                                         TextEditor::TextEditorWidget::CodeSemanticsSelection);
        QCOMPARE(marked.size(), 3);
        for (const TextEditor::TextDocument::ExtraSelection &selection : marked)
            QCOMPARE(selection.cursor.selectedText(), QString("alpha"));

        // And the caret somewhere that is not a symbol marks nothing, so what
        // is asserted above is the symbol rather than the file.
        editor->gotoLine(2, 1);
        updater->update(CppUseSelectionsUpdater::CallType::Synchronous);
        QVERIFY2(TextEditor::viewSelections(
                     editor, TextEditor::TextEditorWidget::CodeSemanticsSelection).isEmpty(),
                 "a caret on nothing still marked something");
    }
};

QObject *createUseSelectionsTest()
{
    return new UseSelectionsTest;
}

#endif // WITH_TESTS

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include "cppuseselectionsupdater.moc"
#endif

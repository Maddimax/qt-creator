// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "emacskeysconstants.h"
#include "emacskeysstate.h"
#include "emacskeystr.h"

#include <coreplugin/icore.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/icontext.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>

#include <extensionsystem/iplugin.h>


#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <utils/qtcassert.h>
#include <utils/temporarydirectory.h>
#include <QScopeGuard>
#include <QTest>
#include <QTextCursor>

QT_BEGIN_NAMESPACE
extern void qt_set_sequence_auto_mnemonic(bool enable);
QT_END_NAMESPACE

using namespace Core;
using namespace TextEditor;
using namespace Utils;

namespace EmacsKeys::Internal {

#ifdef WITH_TESTS
QObject *createEmacsKeysTest();
#endif

static QString plainSelectedText(const QTextCursor &cursor)
{
    // selectedText() returns U+2029 (PARAGRAPH SEPARATOR) instead of newline
    return cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
}

class EmacsKeysPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EmacsKeys.json")

    void initialize() final;

    void editorAboutToClose(Core::IEditor *editor);
    void currentEditorChanged(Core::IEditor *editor);

    void deleteCharacter();       // C-d
    void killWord();              // M-d
    void killLine();              // C-k
    void insertLineAndIndent();   // C-j

    void gotoFileStart();         // M-<
    void gotoFileEnd();           // M->
    void gotoLineStart();         // C-a
    void gotoLineEnd();           // C-e
    void gotoNextLine();          // C-n
    void gotoPreviousLine();      // C-p
    void gotoNextCharacter();     // C-f
    void gotoPreviousCharacter(); // C-b
    void gotoNextWord();          // M-f
    void gotoPreviousWord();      // M-b

    void mark();                  // C-SPC
    void exchangeCursorAndMark(); // C-x C-x
    void copy();                  // M-w
    void cut();                   // C-w
    void yank();                  // C-y

    void scrollHalfDown();        // C-v
    void scrollHalfUp();          // M-v

    QAction *registerAction(Id id, void (EmacsKeysPlugin::*callback)(), const QString &title);
    void genericGoto(QTextCursor::MoveOperation op, bool abortAssist = true);
    void genericVScroll(int direction);

    QHash<Core::IEditor *, EmacsKeysState *> m_stateMap;
    // The view the reader is in. Keyed on the editor rather than on a
    // QPlainTextEdit, so that these keys work in the Qt Quick editor - where
    // there is no such widget and every one of them used to do nothing.
    QPointer<Core::IEditor> m_currentEditor;
    EmacsKeysState *m_currentState = nullptr;
    TextEditorWidget *m_currentBaseTextEditorWidget = nullptr;
};

void EmacsKeysPlugin::initialize()
{
#ifdef WITH_TESTS
    addTestCreator(createEmacsKeysTest);
#endif
    // We have to use this hack here at the moment, because it's the only way to
    // disable Qt Creator menu accelerators aka mnemonics. Many of them get into
    // the way of typical emacs keys, such as: Alt+F (File), Alt+B (Build),
    // Alt+W (Window).
    qt_set_sequence_auto_mnemonic(false);

    connect(EditorManager::instance(), &EditorManager::editorAboutToClose,
            this, &EmacsKeysPlugin::editorAboutToClose);
    connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
            this, &EmacsKeysPlugin::currentEditorChanged);

    registerAction(Constants::DELETE_CHARACTER,
        &EmacsKeysPlugin::deleteCharacter, Tr::tr("Delete Character"));
    registerAction(Constants::KILL_WORD,
        &EmacsKeysPlugin::killWord, Tr::tr("Kill Word"));
    registerAction(Constants::KILL_LINE,
        &EmacsKeysPlugin::killLine, Tr::tr("Kill Line"));
    registerAction(Constants::INSERT_LINE_AND_INDENT,
        &EmacsKeysPlugin::insertLineAndIndent, Tr::tr("Insert New Line and Indent"));

    registerAction(Constants::GOTO_FILE_START,
        &EmacsKeysPlugin::gotoFileStart, Tr::tr("Go to File Start"));
    registerAction(Constants::GOTO_FILE_END,
        &EmacsKeysPlugin::gotoFileEnd, Tr::tr("Go to File End"));
    registerAction(Constants::GOTO_LINE_START,
        &EmacsKeysPlugin::gotoLineStart, Tr::tr("Go to Line Start"));
    registerAction(Constants::GOTO_LINE_END,
        &EmacsKeysPlugin::gotoLineEnd, Tr::tr("Go to Line End"));
    registerAction(Constants::GOTO_NEXT_LINE,
        &EmacsKeysPlugin::gotoNextLine, Tr::tr("Go to Next Line"));
    registerAction(Constants::GOTO_PREVIOUS_LINE,
        &EmacsKeysPlugin::gotoPreviousLine, Tr::tr("Go to Previous Line"));
    registerAction(Constants::GOTO_NEXT_CHARACTER,
        &EmacsKeysPlugin::gotoNextCharacter, Tr::tr("Go to Next Character"));
    registerAction(Constants::GOTO_PREVIOUS_CHARACTER,
        &EmacsKeysPlugin::gotoPreviousCharacter, Tr::tr("Go to Previous Character"));
    registerAction(Constants::GOTO_NEXT_WORD,
        &EmacsKeysPlugin::gotoNextWord, Tr::tr("Go to Next Word"));
    registerAction(Constants::GOTO_PREVIOUS_WORD,
        &EmacsKeysPlugin::gotoPreviousWord, Tr::tr("Go to Previous Word"));

    registerAction(Constants::MARK,
        &EmacsKeysPlugin::mark, Tr::tr("Mark"));
    registerAction(Constants::EXCHANGE_CURSOR_AND_MARK,
        &EmacsKeysPlugin::exchangeCursorAndMark, Tr::tr("Exchange Cursor and Mark"));
    registerAction(Constants::COPY,
        &EmacsKeysPlugin::copy, Tr::tr("Copy"));
    registerAction(Constants::CUT,
        &EmacsKeysPlugin::cut, Tr::tr("Cut"));
    registerAction(Constants::YANK,
        &EmacsKeysPlugin::yank, Tr::tr("Yank"));

    registerAction(Constants::SCROLL_HALF_DOWN,
        &EmacsKeysPlugin::scrollHalfDown, Tr::tr("Scroll Half Screen Down"));
    registerAction(Constants::SCROLL_HALF_UP,
        &EmacsKeysPlugin::scrollHalfUp, Tr::tr("Scroll Half Screen Up"));
}

void EmacsKeysPlugin::editorAboutToClose(IEditor *editor)
{
    if (m_stateMap.contains(editor)) {
        if (m_currentState == m_stateMap[editor])
            m_currentState = nullptr;
        delete m_stateMap[editor];
        m_stateMap.remove(editor);
    }
}

void EmacsKeysPlugin::currentEditorChanged(IEditor *editor)
{
    m_currentEditor = nullptr;
    m_currentState = nullptr;
    m_currentBaseTextEditorWidget = nullptr;
    if (!editor)
        return;

    // A text document is what these commands edit; which view shows it only
    // decides how the caret is asked for.
    if (!qobject_cast<TextEditor::TextDocument *>(editor->document()))
        return;

    m_currentEditor = editor;
    if (!m_stateMap.contains(editor))
        m_stateMap[editor] = new EmacsKeysState(editor);
    m_currentState = m_stateMap[editor];
    m_currentBaseTextEditorWidget = TextEditorWidget::fromEditor(editor);
}

void EmacsKeysPlugin::gotoFileStart()         { genericGoto(QTextCursor::Start); }
void EmacsKeysPlugin::gotoFileEnd()           { genericGoto(QTextCursor::End); }
void EmacsKeysPlugin::gotoLineStart()         { genericGoto(QTextCursor::StartOfLine); }
void EmacsKeysPlugin::gotoLineEnd()           { genericGoto(QTextCursor::EndOfLine); }
void EmacsKeysPlugin::gotoNextLine()          { genericGoto(QTextCursor::Down, false); }
void EmacsKeysPlugin::gotoPreviousLine()      { genericGoto(QTextCursor::Up, false); }
void EmacsKeysPlugin::gotoNextCharacter()     { genericGoto(QTextCursor::Right); }
void EmacsKeysPlugin::gotoPreviousCharacter() { genericGoto(QTextCursor::Left); }
void EmacsKeysPlugin::gotoNextWord()          { genericGoto(QTextCursor::NextWord); }
void EmacsKeysPlugin::gotoPreviousWord()      { genericGoto(QTextCursor::PreviousWord); }

void EmacsKeysPlugin::mark()
{
    if (!m_currentEditor || !m_currentState)
        return;

    m_currentState->beginOwnAction();
    QTextCursor cursor = textCursorOf(m_currentEditor);
    if (m_currentState->mark() == cursor.position()) {
        m_currentState->setMark(-1);
    } else {
        cursor.clearSelection();
        m_currentState->setMark(cursor.position());
        setTextCursorOf(m_currentEditor, cursor);
    }
    m_currentState->endOwnAction(KeysActionOther);
}

void EmacsKeysPlugin::exchangeCursorAndMark()
{
    if (!m_currentEditor || !m_currentState)
        return;

    QTextCursor cursor = textCursorOf(m_currentEditor);
    if (m_currentState->mark() == -1 || m_currentState->mark() == cursor.position())
        return;

    m_currentState->beginOwnAction();
    int position = cursor.position();
    cursor.clearSelection();
    cursor.setPosition(m_currentState->mark(), QTextCursor::KeepAnchor);
    m_currentState->setMark(position);
    setTextCursorOf(m_currentEditor, cursor);
    m_currentState->endOwnAction(KeysActionOther);
}

void EmacsKeysPlugin::copy()
{
    if (!m_currentEditor || !m_currentState)
        return;

    m_currentState->beginOwnAction();
    QTextCursor cursor = textCursorOf(m_currentEditor);
    QApplication::clipboard()->setText(plainSelectedText(cursor));
    cursor.clearSelection();
    setTextCursorOf(m_currentEditor, cursor);
    m_currentState->setMark(-1);
    m_currentState->endOwnAction(KeysActionOther);
}

void EmacsKeysPlugin::cut()
{
    if (!m_currentEditor || !m_currentState)
        return;

    m_currentState->beginOwnAction();
    QTextCursor cursor = textCursorOf(m_currentEditor);
    QApplication::clipboard()->setText(plainSelectedText(cursor));
    cursor.removeSelectedText();
    m_currentState->setMark(-1);
    m_currentState->endOwnAction(KeysActionOther);
}

void EmacsKeysPlugin::yank()
{
    if (!m_currentEditor || !m_currentState)
        return;

    m_currentState->beginOwnAction();
    pasteIn(m_currentEditor);
    m_currentState->setMark(-1);
    m_currentState->endOwnAction(KeysActionOther);
}

void EmacsKeysPlugin::scrollHalfDown() { genericVScroll(1); }
void EmacsKeysPlugin::scrollHalfUp() { genericVScroll(-1); }

void EmacsKeysPlugin::deleteCharacter()
{
    if (!m_currentEditor || !m_currentState)
        return;
    m_currentState->beginOwnAction();
    textCursorOf(m_currentEditor).deleteChar();
    m_currentState->endOwnAction(KeysActionOther);
}

void EmacsKeysPlugin::killWord()
{
    if (!m_currentEditor || !m_currentState)
        return;
    m_currentState->beginOwnAction();
    QTextCursor cursor = textCursorOf(m_currentEditor);
    cursor.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
    if (m_currentState->lastAction() == KeysActionKillWord) {
        QApplication::clipboard()->setText(
            QApplication::clipboard()->text() + plainSelectedText(cursor));
    } else {
        QApplication::clipboard()->setText(plainSelectedText(cursor));
    }
    cursor.removeSelectedText();
    m_currentState->endOwnAction(KeysActionKillWord);
}

void EmacsKeysPlugin::killLine()
{
    if (!m_currentEditor || !m_currentState)
        return;

    m_currentState->beginOwnAction();
    QTextCursor cursor = textCursorOf(m_currentEditor);
    int position = cursor.position();
    cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    if (cursor.position() == position) {
        // empty line
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    }
    if (m_currentState->lastAction() == KeysActionKillLine) {
        QApplication::clipboard()->setText(
            QApplication::clipboard()->text() + plainSelectedText(cursor));
    } else {
        QApplication::clipboard()->setText(plainSelectedText(cursor));
    }
    cursor.removeSelectedText();
    m_currentState->endOwnAction(KeysActionKillLine);
}

void EmacsKeysPlugin::insertLineAndIndent()
{
    if (!m_currentEditor || !m_currentState)
        return;

    m_currentState->beginOwnAction();
    QTextCursor cursor = textCursorOf(m_currentEditor);
    cursor.beginEditBlock();
    cursor.insertBlock();
    // The document rather than the widget: indenting is the language's and the
    // document carries it, so this is the half of C-j that used to be skipped
    // in a view that is not a QPlainTextEdit.
    if (auto * const document
        = qobject_cast<TextEditor::TextDocument *>(m_currentEditor->document())) {
        document->autoIndent(cursor);
    }
    cursor.endEditBlock();
    setTextCursorOf(m_currentEditor, cursor);
    m_currentState->endOwnAction(KeysActionOther);
}

QAction *EmacsKeysPlugin::registerAction(Id id, void (EmacsKeysPlugin::*callback)(),
                                         const QString &title)
{
    auto result = new QAction(title, this);
    ActionManager::registerAction(result, id, Context(Core::Constants::C_GLOBAL), true);
    connect(result, &QAction::triggered, this, callback);
    return result;
}

void EmacsKeysPlugin::genericGoto(QTextCursor::MoveOperation op, bool abortAssist)
{
    if (!m_currentEditor || !m_currentState)
        return;
    m_currentState->beginOwnAction();
    QTextCursor cursor = textCursorOf(m_currentEditor);
    cursor.movePosition(op, m_currentState->mark() != -1 ?
        QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
    setTextCursorOf(m_currentEditor, cursor);
    if (abortAssist && m_currentBaseTextEditorWidget)
        m_currentBaseTextEditorWidget->abortAssist();
    m_currentState->endOwnAction(KeysActionOther);
}

void EmacsKeysPlugin::genericVScroll(int direction)
{
    if (!m_currentEditor || !m_currentState)
        return;

    m_currentState->beginOwnAction();
    scrollHalfPageIn(m_currentEditor, direction);

    // The caret follows only if the scroll left it behind. Scrolling down
    // takes it off the top, so it walks down to catch up, and the other way
    // round - which is what asking where it was drawn used to work out.
    const QTextCursor::MoveMode mode =
        m_currentState->mark() != -1 ?
        QTextCursor::KeepAnchor :
        QTextCursor::MoveAnchor ;
    const QTextCursor::MoveOperation op =
        direction > 0 ? QTextCursor::Down : QTextCursor::Up;

    QTextCursor cursor = textCursorOf(m_currentEditor);
    while (!isPositionVisibleIn(m_currentEditor, cursor.position())) {
        const int previousPosition = cursor.position();
        cursor.movePosition(op, mode);
        if (previousPosition == cursor.position())
            break;
    }
    setTextCursorOf(m_currentEditor, cursor);
    m_currentState->endOwnAction(KeysActionOther);
}

#ifdef WITH_TESTS

// Every one of these keys used to be dead on a C++ file: the plugin looked for
// a QPlainTextEdit in the editor's widget, and the Qt Quick editor has none.
class EmacsKeysTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheKeysReachAViewThatIsNotAWidget()
    {
        Utils::TemporaryDirectory dir("emacskeys-any-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("keys.cpp");
        QVERIFY(file.writeFileContents("int alpha = 1;\nint beta = 2;\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(factory, "no editor factory claims a C++ file");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(true);
        const QScopeGuard closeAll([] { EditorManager::closeAllEditors(false); });

        IEditor * const editor = EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        QVERIFY2(!TextEditorWidget::fromEditor(editor),
                 "the C++ file opened in a widget editor, so this tests nothing");
        auto * const document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        const auto trigger = [](const char *id) {
            Command * const cmd = ActionManager::command(Id::fromName(id));
            QTC_ASSERT(cmd && cmd->action(), return false);
            cmd->action()->trigger();
            return true;
        };

        // Moving the caret: Ctrl+E, to the end of the line it is on. The
        // column gotoLine() takes is zero-based.
        editor->gotoLine(1, 0);
        QCOMPARE(textCursorOf(editor).positionInBlock(), 0);
        QVERIFY(trigger(Constants::GOTO_LINE_END));
        QTRY_COMPARE(textCursorOf(editor).positionInBlock(),
                     QString("int alpha = 1;").size());

        // And editing: Ctrl+K, which takes the rest of the line away.
        editor->gotoLine(2, 5);
        QVERIFY(trigger(Constants::KILL_LINE));
        QTRY_VERIFY2(!document->plainText().contains("beta = 2"),
                     qPrintable("the line was not killed:\n" + document->plainText()));
        QVERIFY2(document->plainText().contains("int alpha = 1;"),
                 "the wrong line was killed");
    }

    void testInsertLineAndIndentIndents_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    // C-j is "insert a new line *and indent it*". The indenting was the one
    // thing here still asking the editor for a TextEditorWidget, and a Qt
    // Quick editor has none - so on a C++ file C-j was a plain Return.
    void testInsertLineAndIndentIndents()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("emacskeys-indent");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("indent.cpp");
        QVERIFY(file.writeFileContents("void f()\n{\n    int a;\n}\n"));

        TextEditorFactory * const factory = TextEditorFactory::preferredFactoryFor(file);
        QVERIFY(factory);
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);
        const QScopeGuard closeAll([] { EditorManager::closeAllEditors(false); });

        IEditor * const editor = EditorManager::openEditor(file);
        QVERIFY(editor);
        QCOMPARE(TextEditorWidget::fromEditor(editor) == nullptr, quick);
        auto * const document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        // At the end of "    int a;", inside the braces: the line C-j makes
        // belongs at the same indent, and a plain Return would leave it at
        // column zero.
        editor->gotoLine(3, 10);
        QCOMPARE(textCursorOf(editor).positionInBlock(), 10);

        Command * const cmd = ActionManager::command(Constants::INSERT_LINE_AND_INDENT);
        QVERIFY(cmd && cmd->action());
        cmd->action()->trigger();

        QTRY_COMPARE(document->document()->blockCount(), 6);
        const QString made = document->document()->findBlockByNumber(3).text();
        QVERIFY2(made.startsWith("    "),
                 qPrintable(QString("the new line came out as [%1]").arg(made)));
        QCOMPARE(textCursorOf(editor).blockNumber(), 3);
    }
};

QObject *createEmacsKeysTest()
{
    return new EmacsKeysTest;
}

#endif // WITH_TESTS

} // EmacsKeys::Internal

#include "emacskeysplugin.moc"

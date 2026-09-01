// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppuseselections_test.h"

#include <utils/textutils.h>

#include <coreplugin/editormanager/ieditor.h>

#include <texteditor/texteditor.h>

#include "cppeditordocument.h"

#include "cppeditorwidget.h"
#include "cppmodelmanager.h"
#include "cpptoolstestcase.h"

#include <QElapsedTimer>
#include <QTest>

// Uses 1-based line and 0-based column.
struct Selection
{
    Selection(int line, int column, int length) : line(line), column(column), length(length) {}

    friend bool operator==(const Selection &l, const Selection &r)
    { return l.line == r.line && l.column == r.column && l.length == r.length; }

    int line;
    int column;
    int length;
};
typedef QList<Selection> SelectionList;
Q_DECLARE_METATYPE(SelectionList)

QT_BEGIN_NAMESPACE
namespace QTest {
template<> char *toString(const Selection &selection)
{
    const QByteArray ba = "Selection("
            + QByteArray::number(selection.line) + ", "
            + QByteArray::number(selection.column) + ", "
            + QByteArray::number(selection.length)
            + ")";
    return qstrdup(ba.data());
}
}
QT_END_NAMESPACE

namespace CppEditor::Internal::Tests {

// Check: If the user puts the cursor on e.g. a function-local variable,
// a type name or a macro use, all occurrences of that entity are highlighted.
class UseSelectionsTestCase : public CppEditor::Tests::TestCase
{
public:
    UseSelectionsTestCase(CppTestDocument &testDocument,
                          const SelectionList &expectedSelections);

private:
    using Selections = QList<TextEditor::TextDocument::ExtraSelection>;
    SelectionList toSelectionList(const Selections &selections) const;
    Selections drawnSelections() const;
    SelectionList waitForUseSelections(bool *hasTimedOut) const;

private:
    // The editor, whichever view it is: where the symbol under the caret is
    // marked is per view, and both views answer viewSelections().
    Core::IEditor *m_editor = nullptr;
    QTextDocument *m_text = nullptr;
};

UseSelectionsTestCase::UseSelectionsTestCase(CppTestDocument &testFile,
                                             const SelectionList &expectedSelections)
{
    QVERIFY(succeededSoFar());

    QVERIFY(testFile.hasCursorMarker());
    testFile.m_source.remove(testFile.m_cursorPosition, 1);

    CppEditor::Tests::TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    testFile.setBaseDirectory(temporaryDir.path());
    testFile.writeToDisk();

    QVERIFY(openCppEditorInAnyView(testFile.filePath(), &m_editor));
    closeEditorAtEndOfTestCase(m_editor);
    auto * const cppDocument = qobject_cast<CppEditorDocument *>(m_editor->document());
    QVERIFY(cppDocument);
    m_text = cppDocument->document();

    QTextCursor caret(m_text);
    caret.setPosition(testFile.m_cursorPosition);
    TextEditor::setTextCursorOf(m_editor, caret);
    QVERIFY(waitForRehighlightedSemanticDocument(cppDocument));

    bool hasTimedOut;
    const SelectionList selections = waitForUseSelections(&hasTimedOut);
    const bool clangCodeModel = CppModelManager::isClangCodeModelActive();
    if (clangCodeModel) {
        QEXPECT_FAIL("local use as macro argument 2 - argument eaten",
                     "https://github.com/clangd/clangd/issues/1844", Abort);
        QEXPECT_FAIL("macro use 1",
                     "clangd does not support document highlights for macros", Abort);
        QEXPECT_FAIL("macro use 2",
                     "clangd does not support document highlights for macros", Abort);
        QEXPECT_FAIL("macro use 3",
                     "clangd does not support document highlights for macros", Abort);
        QEXPECT_FAIL("macro use 4",
                     "clangd does not support document highlights for macros", Abort);
    }
    QVERIFY(!hasTimedOut);
//    for (const Selection &selection : selections)
//        qDebug() << QTest::toString(selection);
    QEXPECT_FAIL("non-local use as macro argument - argument expanded 2",
                 clangCodeModel ? "FIXME: One occurrence comes in twice" : "TODO", Abort);
    QEXPECT_FAIL("local use as macro argument 2 - argument eaten",
                 "expansion takes away the original token", Abort);
    QCOMPARE(selections, expectedSelections);
}

SelectionList UseSelectionsTestCase::toSelectionList(const Selections &selections) const
{
    SelectionList result;
    for (const TextEditor::TextDocument::ExtraSelection &selection : selections) {
        int line, column;
        const int position = qMin(selection.cursor.position(), selection.cursor.anchor());
        Utils::Text::convertPosition(m_text, position, &line, &column);
        result << Selection(line, column, selection.cursor.selectedText().size());
    }
    return result;
}

UseSelectionsTestCase::Selections UseSelectionsTestCase::drawnSelections() const
{
    return TextEditor::viewSelections(m_editor,
                                      TextEditor::TextEditorWidget::CodeSemanticsSelection);
}

SelectionList UseSelectionsTestCase::waitForUseSelections(bool *hasTimedOut) const
{
    QElapsedTimer timer;
    timer.start();
    if (hasTimedOut)
        *hasTimedOut = false;

    Selections selections = drawnSelections();
    while (selections.isEmpty()) {
        if (timer.hasExpired(2500)) {
            if (hasTimedOut)
                *hasTimedOut = true;
            break;
        }
        QCoreApplication::processEvents();
        selections = drawnSelections();
    }

    return toSelectionList(selections);
}

void SelectionsTest::testUseSelections_data()
{
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<SelectionList>("expectedSelections");
    typedef QByteArray _;

    QTest::newRow("local uses")
            << _("int f(int arg) { return @arg; }\n")
            << (SelectionList()
                << Selection(1, 10, 3)
                << Selection(1, 24, 3)
                );

    QTest::newRow("local use as macro argument 1 - argument expanded")
            << _("#define Q_UNUSED(x) (void)x\n"
                 "void f(int arg)\n"
                 "{\n"
                 "    Q_UNUSED(@arg)\n"
                 "}\n")
            << (SelectionList()
                << Selection(2, 11, 3)
                << Selection(4, 13, 3)
                );

    QTest::newRow("local use as macro argument 2 - argument eaten")
            << _("inline void qt_noop(void) {}\n"
                 "#define Q_ASSERT(cond) qt_noop()\n"
                 "void f(char *text)\n"
                 "{\n"
                 "    Q_ASSERT(@text);\n"
                 "}\n")
            << (SelectionList()
                << Selection(3, 13, 4)
                << Selection(5, 13, 4)
                );

    // clangd differentiates between constructor and class.
    SelectionList nonLocalUses;
    if (!CppModelManager::isClangCodeModelActive())
        nonLocalUses << Selection(1, 7, 3);
    nonLocalUses << Selection(1, 13, 3);
    QTest::newRow("non-local uses")
            << _("struct Foo { @Foo(); };\n")
            << nonLocalUses;

    QTest::newRow("non-local use as macro argument - argument expanded 1")
            << _("#define QT_FORWARD_DECLARE_CLASS(name) class name;\n"
                 "QT_FORWARD_DECLARE_CLASS(Class)\n"
                 "@Class *foo;\n")
            << (SelectionList()
                << Selection(2, 25, 5)
                << Selection(3, 0, 5)
                );

    QTest::newRow("non-local use as macro argument - argument expanded 2")
            << _("#define Q_DISABLE_COPY(Class) \\\n"
                 "    Class(const Class &);\\\n"
                 "    Class &operator=(const Class &);\n"
                 "class @Super\n"
                 "{\n"
                 "    Q_DISABLE_COPY(Super)\n"
                 "};\n")
            << (SelectionList()
                << Selection(4, 6, 5)
                << Selection(6, 19, 5)
                );

    const SelectionList macroUseSelections = SelectionList()
            << Selection(1, 8, 3)
            << Selection(2, 0, 3);

    QTest::newRow("macro use 1")
            << _("#define FOO\n"
                 "@FOO\n")
            << macroUseSelections;

    QTest::newRow("macro use 2")
            << _("#define FOO\n"
                 "FOO@\n")
            << macroUseSelections;

    QTest::newRow("macro use 3")
            << _("#define @FOO\n"
                 "FOO\n")
            << macroUseSelections;

    QTest::newRow("macro use 4")
            << _("#define FOO@\n"
                 "FOO\n")
            << macroUseSelections;
}

void SelectionsTest::testUseSelections()
{
    QFETCH(QByteArray, source);
    QFETCH(SelectionList, expectedSelections);

    Tests::CppTestDocument testDocument("file.cpp", source);
    Tests::UseSelectionsTestCase(testDocument, expectedSelections);
}

void SelectionsTest::testSelectionFiltering_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<SelectionList>("original");
    QTest::addColumn<SelectionList>("filtered");

    QTest::addRow("QTCREATORBUG-18659")
            << QString("int main()\n"
                       "{\n"
                       "    [](const Foo &foo) -> Foo {\n"
                       "        return foo;\n"
                       "    };\n"
                       "}\n")
            << SelectionList{{3, 4, 53}}
            << SelectionList{{3, 4, 27}, {4, 8, 11}, {5, 4, 1}};
    QTest::addRow("indentation-selected-in-first-line")
            << QString("int main()\n"
                       "{\n"
                       "    [](const Foo &foo) -> Foo {\n"
                       "        return foo;\n"
                       "    };\n"
                       "}\n")
            << SelectionList{{3, 0, 57}}
            << SelectionList{{3, 4, 27}, {4, 8, 11}, {5, 4, 1}};
}

void SelectionsTest::testSelectionFiltering()
{
    QFETCH(QString, source);
    QFETCH(SelectionList, original);
    QFETCH(SelectionList, filtered);

    QTextDocument doc;
    doc.setPlainText(source);

    const auto convertList = [&doc](const SelectionList &in) {
        QList<QTextEdit::ExtraSelection> out;
        for (const Selection &selIn : in) {
            QTextEdit::ExtraSelection selOut;
            selOut.format.setFontItalic(true);
            const QTextBlock startBlock = doc.findBlockByLineNumber(selIn.line - 1);
            const int startPos = startBlock.position() + selIn.column;
            selOut.cursor = QTextCursor(&doc);
            selOut.cursor.setPosition(startPos);
            selOut.cursor.setPosition(startPos + selIn.length, QTextCursor::KeepAnchor);
            out << selOut;
        }
        return out;
    };

    const QList<QTextEdit::ExtraSelection> expected = convertList(filtered);
    const QList<QTextEdit::ExtraSelection> actual
            = CppEditorWidget::unselectLeadingWhitespace(convertList(original));

    QCOMPARE(actual.length(), expected.length());
    for (int i = 0; i < expected.length(); ++i) {
        const QTextEdit::ExtraSelection &expectedSelection = expected.at(i);
        const QTextEdit::ExtraSelection &actualSelection = actual.at(i);
        QCOMPARE(actualSelection.format, expectedSelection.format);
        QCOMPARE(actualSelection.cursor.document(), expectedSelection.cursor.document());
        QCOMPARE(actualSelection.cursor.position(), expectedSelection.cursor.position());
        QCOMPARE(actualSelection.cursor.anchor(), expectedSelection.cursor.anchor());
    }
}

} // namespace CppEditor::Internal::Tests

// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifdef WITH_TESTS

#include "qmljsindenter_test.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>

#include <texteditor/tabsettings.h>
#include <texteditor/texteditor.h>
#include <texteditor/textdocument.h>

#include <utils/temporarydirectory.h>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QScopeGuard>
#include <QTest>
#include <QTextCursor>

using namespace Core;
using namespace Utils;

namespace QmlJSEditor::Internal {

// Editor-level QML auto-indentation while typing, as the removed Squish test
// suite_QMLS/tst_QMLS08 exercised: type a block out without leading indentation
// and let the editor indent it. Unlike the QmlJSTools plugin test (which drives
// the indenter directly), this runs against a real QML editor - with the QmlJS
// auto-completer active - so it also covers the "electric" closing brace that
// dedents, and the full typing path of either view: the widget's
// keyPressEvent and the Qt Quick view's. It was pinned to the widget view; a
// QML file opens in the Qt Quick one.

class QmlJSIndenterTest final : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void testAutoIndentWhileTyping_data();
    void testAutoIndentWhileTyping();
};

void QmlJSIndenterTest::cleanup()
{
    EditorManager::closeAllEditors(false);
}

void QmlJSIndenterTest::testAutoIndentWhileTyping_data()
{
    QTest::addColumn<bool>("quick");
    QTest::newRow("widget") << false;
    QTest::newRow("quick") << true;
}

void QmlJSIndenterTest::testAutoIndentWhileTyping()
{
    QFETCH(bool, quick);

    TemporaryDirectory tempDir("qtc-qmljs-indent-XXXXXX");
    QVERIFY(tempDir.isValid());
    const FilePath filePath = tempDir.filePath("Typed.qml");
    QVERIFY(filePath.writeFileContents("import QtQuick\nItem {\n}\n"));

    TextEditor::TextEditorFactory * const factory
        = TextEditor::TextEditorFactory::preferredFactoryFor(filePath);
    QVERIFY(factory);
    const bool wasQuick = factory->usesQuickEditor();
    factory->setUsesQuickEditor(quick);
    const QScopeGuard restoreView([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
    IEditor *editor = EditorManager::openEditor(filePath);
    QVERIFY(editor);
    QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);
    auto * const document = qobject_cast<TextEditor::TextDocument *>(editor->document());
    QVERIFY(document);

    TextEditor::TabSettingsData tabSettings(TextEditor::TabSettingsData::SpacesOnlyTabPolicy,
                                            4, 4,
                                            TextEditor::TabSettingsData::ContinuationAlignWithSpaces);
    tabSettings.m_autoDetect = false;
    document->setTabSettings(tabSettings);

    // Put the cursor at the end of the "Item {" line.
    QTextCursor cursor(document->document());
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down);
    cursor.movePosition(QTextCursor::EndOfLine);
    TextEditor::setTextCursorOf(editor, cursor);

    // Keys to whatever takes them for this editor - the widget, or the Quick
    // item, which QTest's key helpers cannot address - one event per key, the
    // way a keyboard delivers them.
    QObject * const target = TextEditor::keyTargetOf(editor);
    QVERIFY2(target, "nothing takes keys for this editor");
    const auto type = [target](Qt::Key key, const QString &text) {
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
        QCoreApplication::sendEvent(target, &release);
    };
    const auto typeText = [&type](const QString &text) {
        for (const QChar c : text)
            type(Qt::Key(c.toUpper().unicode()), QString(c));
    };

    // Type a nested object out. Typing "{" is an electric character: the editor
    // auto-closes the brace and, on the following Return, expands the block and
    // indents its body and closing brace - all through the typing path's
    // electric-character handling. (The matching "}" is inserted by the editor,
    // so it is not typed here.)
    type(Qt::Key_Return, "\r");
    typeText("Rectangle {");
    type(Qt::Key_Return, "\r");
    typeText("width: 10");

    QCOMPARE(document->plainText(), QString(
        "import QtQuick\n"
        "Item {\n"
        "    Rectangle {\n"
        "        width: 10\n"
        "    }\n"
        "}\n"));
}

QObject *createQmlJSIndenterTest()
{
    return new QmlJSIndenterTest;
}

} // namespace QmlJSEditor::Internal

#include "qmljsindenter_test.moc"

#endif // WITH_TESTS

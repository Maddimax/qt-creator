// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "basetexteditmodifier.h"

#include <qmljs/parser/qmljsast_p.h>

#include <qmljstools/qmljsindenter.h>
#include <qmljstools/qmljssettings.h>

#include <qmljseditor/qmljseditordocument.h>
#include <qmljseditor/qmljscomponentfromobjectdef.h>
#include <qmljseditor/qmljscompletionassist.h>

#include <texteditor/tabsettings.h>
#include <utils/changeset.h>
#include <utils/textutils.h>

using namespace QmlDesigner;

BaseTextEditModifier::BaseTextEditModifier(TextEditor::TextDocument *document)
    : PlainTextEditModifier(document->document())
    , m_document{document}
{
}

void BaseTextEditModifier::indentLines(int startLine, int endLine)
{
    if (startLine < 0)
        return;

    if (!m_document)
        return;

    QmlJSEditor::indentQmlJs(textDocument(), startLine, endLine, m_document->tabSettings());
}

void BaseTextEditModifier::indent(int offset, int length)
{
    if (length == 0 || offset < 0 || offset + length >= text().size())
        return;

    int startLine = getLineInDocument(textDocument(), offset);
    int endLine = getLineInDocument(textDocument(), offset + length);

    if (startLine > -1 && endLine > -1)
        indentLines(startLine, endLine);
}

TextEditor::TabSettingsData BaseTextEditModifier::tabSettings() const
{
    if (m_document)
        return m_document->tabSettings();
    return QmlJSTools::globalQmlJSCodeStyle()->tabSettings();
}

bool BaseTextEditModifier::renameId(const QString &oldId, const QString &newId)
{
    if (auto document = qobject_cast<QmlJSEditor::QmlJSEditorDocument *>(m_document)) {
        Utils::ChangeSet changeSet;
        const QList<QmlJS::SourceLocation> locations = document->semanticInfo()
                                                           .idLocations.value(oldId);
        for (const QmlJS::SourceLocation &loc : locations) {
            changeSet.replace(loc.begin(), loc.end(), newId);
        }
        QTextCursor tc = textCursor();
        changeSet.apply(&tc);
        return true;
    }
    return false;
}

static QmlJS::AST::UiObjectDefinition *getObjectDefinition(const QList<QmlJS::AST::Node *> &path, QmlJS::AST::UiQualifiedId *qualifiedId)
{
    QmlJS::AST::UiObjectDefinition *object = nullptr;
    for (int i = path.size() - 1; i >= 0; --i) {
        auto node = path.at(i);
        if (auto objDef =  QmlJS::AST::cast<QmlJS::AST::UiObjectDefinition *>(node)) {
            if (objDef->qualifiedTypeNameId == qualifiedId)
                object = objDef;
        }
    }
    return object;
}

QString BaseTextEditModifier::moveToComponent(int nodeOffset, const QString &importData)
{
    if (auto document = qobject_cast<QmlJSEditor::QmlJSEditorDocument *>(m_document)) {
        auto qualifiedId = QmlJS::AST::cast<QmlJS::AST::UiQualifiedId *>(
            document->semanticInfo().astNodeAt(nodeOffset));
        QList<QmlJS::AST::Node *> path = document->semanticInfo().rangePath(nodeOffset);
        QmlJS::AST::UiObjectDefinition *object = getObjectDefinition(path, qualifiedId);

        if (!object)
            return {};

        return QmlJSEditor::performComponentFromObjectDef(
            document,
            textCursor(),
            document->filePath().toUrlishString(),
            object,
            importData);
    }
    return {};
}

QStringList BaseTextEditModifier::autoComplete(QTextDocument *textDocument, int position, bool explicitComplete)
{
    if (auto document = qobject_cast<QmlJSEditor::QmlJSEditorDocument *>(m_document)) {
        const TextEditor::AssistReason reason = explicitComplete ? TextEditor::ExplicitlyInvoked
                                                                 : TextEditor::ActivationCharacter;
        return QmlJSEditor::qmlJSAutoComplete(textDocument, position, document->filePath(),
                                              reason, document->semanticInfo());
    }
    return {};
}

void BaseTextEditModifier::convertPosition(int pos, int *line, int *column) const
{
    Utils::Text::convertPosition(textDocument(), pos, line, column);
}

#ifdef WITH_TESTS

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <texteditor/texteditor.h>
#include <utils/temporarydirectory.h>

#include <QScopeGuard>
#include <QTest>

namespace QmlDesigner {

// The modifier took the widget editor and read its document through it, so a
// design document could only stand on a widget. Everything it read is the
// document's, and the document is what either view shows.
class BaseTextEditModifierTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheModifierWorksOnEitherViewsDocument_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("widget") << false;
        QTest::newRow("quick") << true;
    }

    void testTheModifierWorksOnEitherViewsDocument()
    {
        QFETCH(bool, quick);

        Utils::TemporaryDirectory dir("qmldesigner-modifier");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("Modified.qml");
        QVERIFY(file.writeFileContents("import QtQuick\n"
                                       "Item {\n"
                                       "      Rectangle { id: box; width: box.height }\n"
                                       "}\n"));

        TextEditor::TextEditorFactory * const factory
            = TextEditor::TextEditorFactory::preferredFactoryFor(file);
        QVERIFY2(factory, "no text editor factory claims a QML file");
        const bool wasQuick = factory->usesQuickEditor();
        const QScopeGuard restore([factory, wasQuick] { factory->setUsesQuickEditor(wasQuick); });
        factory->setUsesQuickEditor(quick);

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(TextEditor::TextEditorWidget::fromEditor(editor) == nullptr, quick);
        auto * const document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);
        auto * const qmlDocument = qobject_cast<QmlJSEditor::QmlJSEditorDocument *>(document);
        QVERIFY(qmlDocument);

        // The document's own tab settings, unlike the globals, so that what the
        // modifier indents with is measured rather than assumed.
        using TabSettingsData = TextEditor::TabSettingsData;
        TabSettingsData tabSettings(TabSettingsData::SpacesOnlyTabPolicy, 2, 2,
                                    TabSettingsData::ContinuationAlignWithSpaces);
        tabSettings.m_autoDetect = false;
        document->setTabSettings(tabSettings);

        BaseTextEditModifier modifier(document);
        QCOMPARE(modifier.tabSettings().m_indentSize, 2);

        // An offset as a line and a column: the line one-based, the column
        // zero-based, which is what Utils::Text::convertPosition() answers and
        // what the widget answered through it.
        int line = 0;
        int column = 0;
        modifier.convertPosition(document->plainText().indexOf("Rectangle"), &line, &column);
        QCOMPARE(line, 3);
        QCOMPARE(column, 6);

        // Renaming an id goes by where the semantic info saw it, so that has to
        // be up to date first - and the rename comes before the re-indent below,
        // which would move those places.
        QTRY_VERIFY2(!qmlDocument->isSemanticInfoOutdated()
                         && qmlDocument->semanticInfo().idLocations.contains("box"),
                     "the semantic info never learnt the id");
        QVERIFY2(modifier.renameId("box", "frame"), "the modifier would not rename the id");
        QCOMPARE(document->plainText().count("frame"), 2);
        QVERIFY2(!document->plainText().contains("box"), qPrintable(document->plainText()));

        // Re-indenting the crooked line uses the document's two spaces.
        modifier.indentLines(2, 2);
        QCOMPARE(document->document()->findBlockByNumber(2).text(),
                 QString("  Rectangle { id: frame; width: frame.height }"));
    }
};

QObject *createBaseTextEditModifierTest()
{
    return new BaseTextEditModifierTest;
}

} // namespace QmlDesigner

#include "basetexteditmodifier.moc"

#endif // WITH_TESTS

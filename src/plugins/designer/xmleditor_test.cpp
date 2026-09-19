// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "designerconstants.h"
#include "formeditor.h"
#include "formwindowfile.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>

#include <texteditor/texteditor.h>

#include <utils/algorithm.h>
#include <utils/temporarydirectory.h>

#include <QKeyEvent>
#include <QScopeGuard>
#include <QTest>

namespace Designer::Internal {

class XmlEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    // The text view of a form, shown in Edit mode: the form's XML, read-only,
    // highlighted, drawn by the Qt Quick text editor. Designer keeps one form
    // beside each view, so the view is not to be split either.
    void testTheXmlViewIsTheQuickEditorAndTakesNoKey()
    {
        Utils::TemporaryDirectory dir("designer-xml-view");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("form.ui");
        QVERIFY(file.writeFileContents(R"(<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>Form</class>
 <widget class="QWidget" name="Form">
  <property name="geometry">
   <rect><x>0</x><y>0</y><width>200</width><height>100</height></rect>
  </property>
 </widget>
 <resources/>
 <connections/>
</ui>
)"));

        Core::IEditor * const editor = Core::EditorManager::openEditor(file);
        QVERIFY(editor);
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });
        QCOMPARE(editor->document()->id(), Utils::Id(Constants::K_DESIGNER_XML_EDITOR_ID));

        QVERIFY2(!qobject_cast<TextEditor::BaseTextEditor *>(editor),
                 "the XML view is still the widget editor");
        QVERIFY2(Utils::anyOf(editor->widget()->findChildren<QWidget *>(),
                              [](QWidget *part) { return part->inherits("QQuickWidget"); }),
                 "the XML view is not drawn by a Qt Quick scene");
        QVERIFY2(!editor->duplicateSupported(),
                 "the XML view could be split, and the second view would have no form");

        auto * const document = qobject_cast<FormWindowFile *>(editor->document());
        QVERIFY2(document, "the view's document is not the form's file");
        QVERIFY2(!document->plainText().isEmpty(), "the form's XML never reached the text");
        QVERIFY2(document->syntaxHighlighter(), "the XML is shown unhighlighted");

        // Typed at the view, which must refuse it: the file is edited in
        // Design mode. Delivered straight to where keys go, so this is about
        // the view being read-only and not about focus.
        const QString before = document->plainText();
        QObject * const target = TextEditor::keyTargetOf(editor);
        QVERIFY(target);
        QKeyEvent typed(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
        QCoreApplication::sendEvent(target, &typed);
        QVERIFY2(document->plainText() == before, "the XML view took a keystroke");
    }
};

QObject *createXmlEditorTest()
{
    return new XmlEditorTest;
}

} // namespace Designer::Internal

#include "xmleditor_test.moc"

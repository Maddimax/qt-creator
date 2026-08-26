// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quicktexteditor.h"

#include "codesource.h"
#include "textdocument.h"
#include "texteditorconstants.h"
#include "texteditortr.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/editormanager/ieditorfactory.h>

#include <qtcquick/qtcquickwidget.h>

#include <utils/mimeutils.h>
#include <utils/temporarydirectory.h>

#include <QQuickItem>
#include <QQuickWidget>
#include <QScopeGuard>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <memory>

namespace TextEditor::Internal {

const char QUICK_TEXT_EDITOR_ID[] = "TextEditor.QuickTextEditor";

// A view of a document somebody else owns. CodeDocument opens a file and
// CodeBuffer holds text of its own; an editor's document is opened by the
// editor manager before the editor is ever shown, so this only points at it.
class AdoptedSource final : public CodeSource
{
public:
    explicit AdoptedSource(TextDocument *document)
        : m_document(document)
    {}

    TextDocument *textDocument() const final { return m_document; }

private:
    TextDocument * const m_document;
};

class QuickTextEditor final : public Core::IEditor
{
public:
    QuickTextEditor()
        : m_document(new TextDocument(QUICK_TEXT_EDITOR_ID))
        , m_source(std::make_unique<AdoptedSource>(m_document.get()))
    {
        auto widget = new QtcQuick::QuickWidget;
        // Set before the source: the form's root property is required, and a
        // required property has to be there when the component is created.
        widget->quickWidget()->setInitialProperties(
            {{"source", QVariant::fromValue(m_source.get())}});
        widget->setSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/MainEditor.qml"));

        setContext(Core::Context(QUICK_TEXT_EDITOR_ID));
        setWidget(widget);
    }

    Core::IDocument *document() const final { return m_document.get(); }
    QWidget *toolBar() final { return nullptr; }

private:
    // Shared because a duplicated editor would show the same document; the
    // editor manager is what decides that, not this.
    std::shared_ptr<TextDocument> m_document;
    std::unique_ptr<AdoptedSource> m_source;
};

class QuickTextEditorFactory final : public Core::IEditorFactory
{
public:
    QuickTextEditorFactory()
    {
        setId(QUICK_TEXT_EDITOR_ID);
        setDisplayName(Tr::tr("Code Editor (Qt Quick)"));
        // The same mime type the plain text editor takes, so a text file can be
        // opened in this from Open With. Registered after it - the default for
        // a mime type is the first factory that claims it - because this one
        // has no folding, no marks and no wrapping yet.
        addMimeType(QLatin1String(Constants::C_TEXTEDITOR_MIMETYPE_TEXT));
        setEditorCreator([] { return new QuickTextEditor; });
    }
};

void setupQuickTextEditor()
{
    static QuickTextEditorFactory theQuickTextEditorFactory;
    Q_UNUSED(theQuickTextEditorFactory)
}

#ifdef WITH_TESTS

class QuickTextEditorTest final : public QObject
{
    Q_OBJECT

private slots:
    // The one thing this must not do while it is unfinished: become what a
    // text file opens in. It is offered beside the plain text editor, and the
    // default for a mime type is the first factory that claims it.
    void testItIsOfferedWithoutBecomingTheDefault()
    {
        const Utils::MimeType text = Utils::mimeTypeForName(
            QLatin1String(Constants::C_TEXTEDITOR_MIMETYPE_TEXT));
        QVERIFY(text.isValid());

        const Core::EditorFactories factories = Core::IEditorFactory::defaultEditorFactories(text);
        QStringList ids;
        for (Core::IEditorFactory * const factory : factories)
            ids << factory->id().toString();

        QVERIFY2(ids.contains(QLatin1String(QUICK_TEXT_EDITOR_ID)),
                 qPrintable("not offered for a text file at all: " + ids.join(", ")));
        QVERIFY2(ids.first() != QLatin1String(QUICK_TEXT_EDITOR_ID),
                 "the unfinished editor is what a text file now opens in");
    }

    // Opening a file goes through the editor manager, which creates the editor
    // and then opens its document - so the form has to show a document it did
    // not open. This is what says the two are the same one.
    void testTheFormShowsTheDocumentTheEditorManagerOpened()
    {
        Utils::TemporaryDirectory dir("quick-editor");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("hello.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\ngamma\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] {
            Core::EditorManager::closeEditors({editor});
        });

        QCOMPARE(editor->document()->filePath(), file);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(document->plainText(), QString("alpha\nbeta\ngamma\n"));

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY2(quick, "the editor built no Qt Quick form");
        // A form whose component failed to load is Ready's opposite and has no
        // root object, which is a blank editor rather than an absent one.
        QCOMPARE(quick->status(), QQuickWidget::Ready);
        QVERIFY(quick->rootObject());

        // And it is showing *that* document rather than one of its own.
        QObject * const viewport = quick->rootObject()->findChild<QObject *>("codeViewport");
        QVERIFY(viewport);
        auto * const shown = viewport->property("document").value<CodeSource *>();
        QVERIFY2(shown, "the form is showing no document");
        QCOMPARE(shown->textDocument(), document);
    }
};

QObject *createQuickTextEditorTest()
{
    return new QuickTextEditorTest;
}

#endif // WITH_TESTS

} // namespace TextEditor::Internal

#ifdef WITH_TESTS
#include "quicktexteditor.moc"
#endif

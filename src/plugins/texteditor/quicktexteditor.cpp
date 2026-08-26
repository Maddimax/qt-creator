// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quicktexteditor.h"

#include "codesource.h"
#include "textdocument.h"
#include "textdocumentlayout.h"
#include "textviewport.h"
#include "texteditorconstants.h"
#include "texteditortr.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/editormanager/ieditorfactory.h>

#include <qtcquick/qtcquickwidget.h>

#include "fontsettings.h"

#include <coreplugin/find/basetextfind.h>
#include <coreplugin/find/ifindsupport.h>

#include <utils/aggregate.h>
#include <utils/theme/theme.h>
#include <utils/mimeutils.h>
#include <utils/temporarydirectory.h>

#include <QDataStream>
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

const char QUICK_FIND_HIGHLIGHTS[] = "TextEditor.QuickTextEditor.FindResults";

// C_SEARCH_RESULT is one of the scheme's *overlay* categories, so its
// QTextCharFormat is deliberately near-empty - FontSettingsData::
// toTextCharFormat() drops the foreground for those, because the widget editor
// paints them as an overlay rather than as a format. This draws a format range,
// so it takes the same colour the overlay does, darkened the same way.
//
// And it falls back rather than leaving the format empty: a scheme that names
// no search colour would otherwise highlight nothing at all, silently. The
// same trap as an unset current-line brush, met a third time.
static QTextCharFormat searchResultFormat(const FontSettingsData &fonts)
{
    const QBrush brush = fonts.toTextCharFormat(C_SEARCH_RESULT).background();
    const bool named = brush.style() != Qt::NoBrush && brush.color().isValid();

    QTextCharFormat format;
    format.setBackground(named ? brush.color().darker(120)
                               : Utils::creatorColor(
                                     Utils::Theme::TextEditor_SearchResult_ScrollBarColor));
    return format;
}

// Ctrl+F, over a viewport rather than over a widget. BaseTextFindBase asks for
// five things - a cursor, a document, whether it is read-only, and a widget to
// anchor the find bar to - none of which needs the editor to *be* a widget.
class QuickTextFind final : public Core::BaseTextFindBase
{
public:
    QuickTextFind(TextViewport *viewport, QWidget *host)
        : m_viewport(viewport)
        , m_host(host)
    {
        // BaseTextFindBase draws nothing itself: it asks, and whoever can draw
        // answers. This is that answer.
        setResultHighlightingEnabled(true);
        connect(this, &Core::BaseTextFindBase::highlightAllRequested,
                this, &QuickTextFind::highlightMatches);
    }

private:
    QTextCursor textCursor() const final
    {
        return m_viewport ? m_viewport->textCursor() : QTextCursor();
    }

    void setTextCursor(const QTextCursor &cursor) final
    {
        if (m_viewport)
            m_viewport->setTextCursor(cursor);
    }

    QTextDocument *document() const final
    {
        TextDocument * const doc = m_viewport && m_viewport->document()
                                       ? m_viewport->document()->textDocument()
                                       : nullptr;
        return doc ? doc->document() : nullptr;
    }

    bool isReadOnly() const final { return !m_viewport || m_viewport->isReadOnly(); }
    QWidget *widget() const final { return m_host; }

    void highlightMatches(const QString &text, Utils::FindFlags flags)
    {
        QTextDocument * const doc = document();
        if (!m_viewport || !doc)
            return;

        QList<TextViewport::Highlight> found;
        if (!text.isEmpty()) {
            const QTextCharFormat format
                = searchResultFormat(m_viewport->document()->textDocument()->fontSettings());
            const QRegularExpression expression
                = Core::BaseTextFindBase::regularExpression(text, flags);
            QTextDocument::FindFlags options
                = flags & Utils::FindCaseSensitively ? QTextDocument::FindCaseSensitively
                                                     : QTextDocument::FindFlags();
            if (flags & Utils::FindWholeWords)
                options |= QTextDocument::FindWholeWords;

            QTextCursor at(doc);
            while (!(at = doc->find(expression, at, options)).isNull()) {
                if (inScope(at))
                    found.append({at.selectionStart(), at.selectionEnd(), format});
                if (at.selectionEnd() == at.selectionStart())
                    at.movePosition(QTextCursor::NextCharacter); // an empty match never advances
            }
        }
        m_viewport->setHighlights(QUICK_FIND_HIGHLIGHTS, found);
    }

    const QPointer<TextViewport> m_viewport;
    QWidget * const m_host;
};

class QuickTextEditor final : public Core::IEditor
{
public:
    QuickTextEditor()
        : QuickTextEditor(std::shared_ptr<TextDocument>(new TextDocument(QUICK_TEXT_EDITOR_ID)))
    {}

    // A split view is two editors on one document, so duplicating shares it
    // rather than opening the file again.
    explicit QuickTextEditor(std::shared_ptr<TextDocument> document)
        : m_document(std::move(document))
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

        // Ctrl+F reaches an editor by asking its widget for an IFindSupport,
        // so this has to hang off the widget rather than off the editor.
        if (TextViewport * const view = viewport())
            Utils::Aggregation::aggregate({widget, new QuickTextFind(view, widget)});
    }

    Core::IDocument *document() const final { return m_document.get(); }
    QWidget *toolBar() final { return nullptr; }

    Core::IEditor *duplicate() final { return new QuickTextEditor(m_document); }

    int currentLine() const final
    {
        TextViewport * const view = viewport();
        return view ? view->cursorLine() : 0;
    }

    int currentColumn() const final
    {
        TextViewport * const view = viewport();
        return view ? view->cursorColumn() : 0;
    }

    // What every jump into a file goes through: a search result, a compiler
    // message, go-to-definition, the locator.
    void gotoLine(int line, int column, bool centerLine) final
    {
        if (TextViewport * const view = viewport())
            view->gotoLine(line, column, centerLine);
    }

    // Where the reader was, so that closing and reopening - or stepping back
    // through the navigation history - returns them to it. A private format:
    // only this editor is ever handed it back.
    QByteArray saveState() const final
    {
        TextViewport * const view = viewport();
        if (!view)
            return {};

        QByteArray state;
        QDataStream stream(&state, QIODevice::WriteOnly);
        stream << kStateVersion << view->cursorPosition() << view->scrollY();
        return state;
    }

    void restoreState(const QByteArray &state) final
    {
        TextViewport * const view = viewport();
        if (!view || state.isEmpty())
            return;

        QDataStream stream(state);
        int version = 0;
        stream >> version;
        if (version != kStateVersion)
            return;

        int position = 0;
        qreal scrollY = 0;
        stream >> position >> scrollY;
        view->setCursorPosition(position);
        // After the cursor: setting it scrolls to it, and where the reader
        // left the view is the more specific answer.
        view->setScrollY(scrollY);
    }

private:
    static constexpr int kStateVersion = 1;

    // The form's viewport. Found rather than held: the QML owns it, and it
    // does not exist until the component has been created.
    TextViewport *viewport() const
    {
        auto * const quick = static_cast<QtcQuick::QuickWidget *>(widget());
        QQuickItem * const root = quick->quickWidget()->rootObject();
        return root ? root->findChild<TextViewport *>() : nullptr;
    }

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
        // still has no find, no completion and no wrapping.
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

    // Every jump into a file - a search result, a compiler message,
    // go-to-definition, the locator - is EditorManager asking the editor to
    // go to a line. An editor that does not answer opens at the top instead,
    // which looks like the feature is broken rather than the editor.
    void testGoingToALineIsWhereTheEditorSaysItIs()
    {
        Utils::TemporaryDirectory dir("quick-editor-goto");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("jump.txt");
        QString contents;
        // Indented, because column zero is supposed to mean "the code on this
        // line" - and on an unindented line that is the same as the margin,
        // so an unindented file cannot tell the two apart.
        for (int i = 0; i < 200; ++i)
            contents += QString("    line %1\n").arg(i);
        QVERIFY(file.writeFileContents(contents.toUtf8()));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY2(editor, "the editor manager opened nothing");
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);

        QCOMPARE(editor->currentLine(), 1);
        QCOMPARE(editor->currentColumn(), 1);

        editor->gotoLine(120, 3);
        QCOMPARE(editor->currentLine(), 120);
        QCOMPARE(editor->currentColumn(), 4);
        // And the line is on screen, not merely under the caret. One condition,
        // not two: "the first visible line is at most 119" is true of an
        // editor that never scrolled at all.
        const auto onScreen = [viewport] {
            return viewport->firstVisibleLine() <= 119
                   && viewport->firstVisibleLine() + viewport->visibleLineCount() > 119;
        };
        QTRY_VERIFY2(onScreen(),
                     qPrintable(QString("line 120 is not on screen: rows %1 to %2, height %3")
                                    .arg(viewport->firstVisibleLine())
                                    .arg(viewport->firstVisibleLine() + viewport->visibleLineCount())
                                    .arg(viewport->height())));

        // Column zero means the line, so it lands on the code rather than in
        // the indentation before it.
        editor->gotoLine(50, 0);
        QCOMPARE(editor->currentLine(), 50);
        QCOMPARE(editor->currentColumn(), 5);

        // A line inside a fold is still a line someone can be sent to, so
        // going there has to open the fold rather than scroll to where the
        // line would have been.
        QTextDocument * const text = document->document();
        auto * const layout = qobject_cast<TextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);
        for (int i = 61; i <= 80; ++i)
            TextBlockUserData::setFoldingIndent(text->findBlockByNumber(i), 1);
        TextBlockUserData::doFoldOrUnfold(text->findBlockByNumber(60), /*unfold=*/false);
        layout->requestUpdate();
        QTRY_VERIFY(!text->findBlockByNumber(70).isVisible());

        editor->gotoLine(71, 0);
        QVERIFY2(text->findBlockByNumber(70).isVisible(),
                 "going to a folded line left it folded, so the caret is nowhere");
        QCOMPARE(editor->currentLine(), 71);

    }

    // Closing and reopening a file, or stepping back through the navigation
    // history, puts the reader back where they were - which is the editor
    // manager handing the editor its own state again.
    void testTheEditorRemembersWhereTheReaderWas()
    {
        Utils::TemporaryDirectory dir("quick-editor-state");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("remembered.txt");
        QString contents;
        for (int i = 0; i < 200; ++i)
            contents += QString("line %1\n").arg(i);
        QVERIFY(file.writeFileContents(contents.toUtf8()));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}); });

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 3);

        editor->gotoLine(150, 0);
        QCOMPARE(editor->currentLine(), 150);
        const qreal wasScrolledTo = viewport->scrollY();
        QVERIFY(wasScrolledTo > 0);

        const QByteArray state = editor->saveState();
        QVERIFY2(!state.isEmpty(), "the editor saved nothing to come back to");

        editor->gotoLine(1, 0);
        QCOMPARE(editor->currentLine(), 1);

        editor->restoreState(state);
        QCOMPARE(editor->currentLine(), 150);
        // The view too, not only the caret: coming back to a line that is
        // technically on screen at the very bottom is not coming back.
        QCOMPARE(viewport->scrollY(), wasScrolledTo);
    }

    // Ctrl+F. The find bar reaches an editor by asking its widget for an
    // IFindSupport, so an editor that aggregates none simply does nothing when
    // the user presses it - no error, no bar, no clue.
    void testFindMovesTheCaretAndMarksEveryMatch()
    {
        Utils::TemporaryDirectory dir("quick-editor-find");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("searchable.txt");
        QVERIFY(file.writeFileContents("alpha beta\ngamma beta\ndelta\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        // Without the second argument, closing after the replace below asks
        // about the modified document - a modal dialog in a test run.
        const QScopeGuard closeIt(
            [editor] { Core::EditorManager::closeEditors({editor}, false); });

        auto * const find = Utils::Aggregation::query<Core::IFindSupport>(editor->widget());
        QVERIFY2(find, "the editor offers no find support, so Ctrl+F does nothing in it");
        QVERIFY(find->supportsReplace());

        auto * const quick = editor->widget()->findChild<QQuickWidget *>();
        QVERIFY(quick && quick->rootObject());
        auto * const viewport = quick->rootObject()->findChild<TextViewport *>();
        QVERIFY(viewport);
        QTRY_VERIFY(viewport->visibleLineCount() > 2);

        // Stepping selects the match rather than merely scrolling to it.
        viewport->setCursorPosition(0);
        QCOMPARE(find->findStep("beta", {}), Core::IFindSupport::Found);
        QCOMPARE(viewport->selectionStart(), 6);
        QCOMPARE(viewport->selectionEnd(), 10);

        // And again takes the next one, on the line below.
        QCOMPARE(find->findStep("beta", {}), Core::IFindSupport::Found);
        QCOMPARE(viewport->selectionStart(), 17);

        // Highlighting all of them reaches the viewport, and through it what
        // is drawn - the point being that the reader can see where the other
        // matches are without stepping to each one.
        find->highlightAll("beta", {});
        QTRY_COMPARE(viewport->highlights(QUICK_FIND_HIGHLIGHTS).size(), 2);

        // Both matches, and each one carrying something that will actually
        // show. C_SEARCH_RESULT is an overlay category whose QTextCharFormat
        // is near-empty by design, and an empty format range draws nothing
        // while looking exactly like a highlight that worked.
        const QList<TextViewport::Highlight> marks = viewport->highlights(QUICK_FIND_HIGHLIGHTS);
        QCOMPARE(marks.size(), 2);
        QCOMPARE(marks.at(0).start, 6);
        QCOMPARE(marks.at(0).end, 10);
        for (const TextViewport::Highlight &mark : marks) {
            QVERIFY2(mark.format.background().style() != Qt::NoBrush
                         && mark.format.background().color().isValid(),
                     "a match is highlighted in nothing, so nobody can see it");
        }

        // That these reach the scene graph is the viewport's own test. Core
        // clears a find's highlights as soon as its widget stops being the
        // current find target, so a test that spun the event loop here would
        // be measuring focus rather than drawing.

        find->clearHighlights();
        QVERIFY(viewport->highlights(QUICK_FIND_HIGHLIGHTS).isEmpty());

        // Replacing writes through the document, so undo can take it back.
        find->replaceStep("beta", "delta", {});
        auto * const document = qobject_cast<TextDocument *>(editor->document());
        QVERIFY(document);
        QVERIFY2(document->plainText().contains("gamma delta"),
                 qPrintable("replace did nothing: " + document->plainText()));
    }

    // A split view is two editors on one document. Duplicating has to share
    // the document rather than open the file twice, or an edit in one half
    // does not appear in the other.
    void testASplitShowsTheSameDocumentTwice()
    {
        Utils::TemporaryDirectory dir("quick-editor-split");
        QVERIFY(dir.isValid());
        const Utils::FilePath file = dir.filePath("shared.txt");
        QVERIFY(file.writeFileContents("alpha\nbeta\n"));

        Core::IEditor * const editor
            = Core::EditorManager::openEditor(file, QUICK_TEXT_EDITOR_ID);
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}); });

        std::unique_ptr<Core::IEditor> other(editor->duplicate());
        QVERIFY2(other, "the editor cannot be split");
        QCOMPARE(other->document(), editor->document());
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

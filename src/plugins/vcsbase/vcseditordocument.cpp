// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcseditordocument.h"

#include "vcsbaseeditorconfig.h"
#include "vcsbaseplugin.h"
#include "vcsbasetr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/vcsmanager.h>

#include <cpaster/codepasterservice.h>

#include <extensionsystem/pluginmanager.h>

#include <texteditor/texteditor.h>

#include <utils/qtcassert.h>
#include <utils/stringutils.h>

#include <QAction>
#include <QDesktopServices>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

using namespace Utils;

namespace VcsBase {

namespace Internal {

// Whichever of \a pattern, a Jira key and a Gerrit Change-Id the cursor's
// column falls in, on the cursor's line; the widget editor's URL handler, as
// it was, minus the widget.
static UrlUnderCursor underCursorMatching(const QTextCursor &cursor,
                                          const QRegularExpression &pattern)
{
    static const QRegularExpression jira("(Fixes|Task-number): ([A-Z]+-[0-9]+)");
    static const QRegularExpression gerrit("Change-Id: (I[a-f0-9]{40})");

    UrlUnderCursor found;
    QTextCursor line = cursor;
    line.select(QTextCursor::LineUnderCursor);
    if (!line.hasSelection())
        return found;
    const QString text = line.selectedText();
    const int column = cursor.columnNumber();
    struct {
        const QRegularExpression &pattern;
        int matchNumber;
        QString urlPrefix;
    } const candidates[] = {
        {pattern, 0, ""},
        {jira, 2, QString("%1/browse/").arg(Core::Constants::QT_JIRA_URL)},
        {gerrit, 1, "https://codereview.qt-project.org/r/"},
    };
    for (const auto &candidate : candidates) {
        QRegularExpressionMatchIterator i = candidate.pattern.globalMatch(text);
        while (i.hasNext()) {
            const QRegularExpressionMatch match = i.next();
            const int start = match.capturedStart(candidate.matchNumber);
            const QString url = match.captured(candidate.matchNumber);
            if (start <= column && column < start + url.size()) {
                found.startColumn = start;
                found.length = int(url.size());
                found.url = candidate.urlPrefix + url;
                return found;
            }
        }
    }
    return found;
}

UrlUnderCursor urlUnderCursor(const QTextCursor &cursor)
{
    static const QRegularExpression http("https?\\://[^\\s]+");
    return underCursorMatching(cursor, http);
}

UrlUnderCursor emailUnderCursor(const QTextCursor &cursor)
{
    static const QRegularExpression mail("[a-zA-Z0-9_\\.-]+@[^@ ]+\\.[a-zA-Z]+");
    return underCursorMatching(cursor, mail);
}

} // namespace Internal

int VcsEditorSections::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant VcsEditorSections::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    switch (role) {
    case Qt::DisplayRole:
        return m_entries.at(index.row());
    case LineRole:
        return m_lines.at(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> VcsEditorSections::roleNames() const
{
    return {{Qt::DisplayRole, "display"}, {LineRole, "line"}};
}

QStringList VcsEditorSections::entries() const
{
    return m_entries;
}

QList<int> VcsEditorSections::lines() const
{
    return m_lines;
}

int VcsEditorSections::sectionOfLine(int line) const
{
    const auto it = std::upper_bound(m_lines.cbegin(), m_lines.cend(), line);
    return int(std::distance(m_lines.cbegin(), it)) - 1;
}

void VcsEditorSections::reset(const QStringList &entries, const QList<int> &lines)
{
    QTC_ASSERT(entries.size() == lines.size(), return);
    beginResetModel();
    m_entries = entries;
    m_lines = lines;
    endResetModel();
}

namespace Internal {

// The sections as the choice the tool bar shows: the row the caret is in is
// current, choosing a row goes to its first line. Nothing here is a pick the
// language would otherwise have made differently, so there is nothing to
// clear and the view offers no way back.
class VcsSectionsChoice final : public TextEditor::ToolBarChoice
{
public:
    VcsSectionsChoice(VcsEditorDocument *document, VcsEditorSections *sections)
        : ToolBarChoice(document)
        , m_document(document)
        , m_sections(sections)
    {
        connect(sections, &QAbstractItemModel::modelReset, this, &ToolBarChoice::changed);
    }

    QAbstractItemModel *model() const override { return m_sections; }
    int currentIndex() const override
    {
        return qMax(0, m_sections->sectionOfLine(m_caretLine - 1));
    }
    QString toolTip() const override { return Tr::tr("Go to an entry"); }
    bool isAvailable() const override { return m_sections->rowCount() > 0; }
    bool isChosen() const override { return false; }
    void clearChoice() override {}

    // In the view showing the document - the current editor where that is
    // the one, else whichever the editor manager has - with a way back, as
    // the widget editor's combo box does it.
    void choose(int index) override
    {
        const QList<int> lines = m_sections->lines();
        if (index < 0 || index >= lines.size())
            return;
        Core::IEditor *editor = Core::EditorManager::currentEditor();
        if (!editor || editor->document() != m_document)
            editor = Core::DocumentModel::editorsForDocument(m_document).value(0);
        if (!editor)
            return;
        const int lineNumber = lines.at(index) + 1;
        if (lineNumber == editor->currentLine())
            return;
        Core::EditorManager::addCurrentPositionToNavigationHistory();
        editor->gotoLine(lineNumber, 0);
    }

    void followCaret(int line) override
    {
        if (m_caretLine == line)
            return;
        const int was = currentIndex();
        m_caretLine = line;
        if (currentIndex() != was)
            emit changed();
    }

private:
    VcsEditorDocument * const m_document;
    VcsEditorSections * const m_sections;
    int m_caretLine = 1;
};

// What is under the cursor that a plain click can do something with, asked
// in the order the widget editor asks its handlers: a change to describe,
// a URL to open, an address to mail.
static TextEditor::ActionLink actionLinkAt(VcsEditorDocument *document, const QTextCursor &cursor)
{
    TextEditor::ActionLink action;
    const int blockStart = cursor.block().position();

    const VcsBaseEditorParameters &parameters = document->parameters();
    if (parameters.changeUnderCursor) {
        const QString change = parameters.changeUnderCursor(cursor);
        if (!change.isEmpty()) {
            QTextCursor word = cursor;
            word.select(QTextCursor::WordUnderCursor);
            action.linkTextStart = word.selectionStart();
            action.linkTextEnd = word.selectionEnd();
            action.activate = [document = QPointer<VcsEditorDocument>(document), change] {
                if (!document)
                    return;
                const auto &describe = document->parameters().describeFunc;
                if (describe)
                    describe(VcsBase::source(document), change);
            };
            return action;
        }
    }

    const UrlUnderCursor url = urlUnderCursor(cursor);
    if (url.isValid()) {
        action.linkTextStart = blockStart + url.startColumn;
        action.linkTextEnd = action.linkTextStart + url.length;
        action.activate = [url = url.url] { QDesktopServices::openUrl(QUrl(url)); };
        return action;
    }

    const UrlUnderCursor mail = emailUnderCursor(cursor);
    if (mail.isValid()) {
        action.linkTextStart = blockStart + mail.startColumn;
        action.linkTextEnd = action.linkTextStart + mail.length;
        action.activate = [address = mail.url] {
            QDesktopServices::openUrl(QUrl("mailto:" + address));
        };
        return action;
    }
    return action;
}

class VcsEditorDocumentPrivate
{
public:
    VcsBaseEditorParameters parameters;
    FilePath workingDirectory;
    int firstLineNumber = -1;
    int defaultLineNumber = -1;
    QString annotateRevisionTextFormat;
    QString annotatePreviousRevisionTextFormat;
    bool fileLogAnnotateEnabled = false;
    QPointer<VcsBaseEditorConfig> config;
    QRegularExpression diffFilePattern;
    QRegularExpression logEntryPattern;
    Annotation annotation;
    VcsEditorDocument::BlockToString fileNameForDiffHeader;
    VcsEditorDocument::BlockToString revisionSubject;
    VcsEditorSections *sections = nullptr;
    VcsSectionsChoice *choice = nullptr;
    QList<QAction *> contextActions;
};

} // namespace Internal

static void regexpFromString(
        const QString &pattern,
        QRegularExpression *regexp,
        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption)
{
    const QRegularExpression re(pattern, options);
    QTC_ASSERT(re.isValid() && re.captureCount() >= 1, return);
    *regexp = re;
}

VcsEditorDocument::VcsEditorDocument(const VcsBaseEditorParameters &parameters)
    : TextDocument(parameters.id)
    , d(new Internal::VcsEditorDocumentPrivate)
{
    d->parameters = parameters;
    d->annotateRevisionTextFormat = Tr::tr("Annotate \"%1\"");
    d->sections = new VcsEditorSections(this);
    setMimeType(parameters.mimeType);
    setSuspendAllowed(false);
    // The sections follow the text. A log or a diff has them; the others
    // have nothing to find, and so nothing to offer in the tool bar either.
    if (parameters.type == LogOutput || parameters.type == DiffOutput) {
        connect(this, &TextDocument::contentsChanged, this, &VcsEditorDocument::updateSections);
        d->choice = new Internal::VcsSectionsChoice(this, d->sections);
    }
    // What a plain click does on a change, a URL or an address - in a log or
    // an annotation, which is where the widget editor's handlers are asked.
    if (parameters.type == LogOutput || parameters.type == AnnotateOutput) {
        setActionLinkFinder([this](TextEditor::TextDocument *, const QTextCursor &cursor) {
            return Internal::actionLinkAt(this, cursor);
        });
    }
}

VcsEditorDocument::~VcsEditorDocument()
{
    delete d;
}

const VcsBaseEditorParameters &VcsEditorDocument::parameters() const
{
    return d->parameters;
}

EditorContentType VcsEditorDocument::contentType() const
{
    return d->parameters.type;
}

FilePath VcsEditorDocument::workingDirectory() const
{
    return d->workingDirectory;
}

void VcsEditorDocument::setWorkingDirectory(const FilePath &workingDirectory)
{
    d->workingDirectory = workingDirectory;
}

int VcsEditorDocument::firstLineNumber() const
{
    return d->firstLineNumber;
}

void VcsEditorDocument::setFirstLineNumber(int firstLineNumber)
{
    d->firstLineNumber = firstLineNumber;
}

int VcsEditorDocument::defaultLineNumber() const
{
    return d->defaultLineNumber;
}

void VcsEditorDocument::setDefaultLineNumber(int line)
{
    d->defaultLineNumber = line;
}

QString VcsEditorDocument::annotateRevisionTextFormat() const
{
    return d->annotateRevisionTextFormat;
}

void VcsEditorDocument::setAnnotateRevisionTextFormat(const QString &format)
{
    d->annotateRevisionTextFormat = format;
}

QString VcsEditorDocument::annotatePreviousRevisionTextFormat() const
{
    return d->annotatePreviousRevisionTextFormat;
}

void VcsEditorDocument::setAnnotatePreviousRevisionTextFormat(const QString &format)
{
    d->annotatePreviousRevisionTextFormat = format;
}

bool VcsEditorDocument::isFileLogAnnotateEnabled() const
{
    return d->fileLogAnnotateEnabled;
}

void VcsEditorDocument::setFileLogAnnotateEnabled(bool enabled)
{
    d->fileLogAnnotateEnabled = enabled;
}

VcsBaseEditorConfig *VcsEditorDocument::editorConfig() const
{
    return d->config;
}

void VcsEditorDocument::setEditorConfig(VcsBaseEditorConfig *config)
{
    d->config = config;
}

void VcsEditorDocument::setDiffFilePattern(const QString &pattern)
{
    regexpFromString(pattern, &d->diffFilePattern);
    updateSections();
}

QRegularExpression VcsEditorDocument::diffFilePattern() const
{
    return d->diffFilePattern;
}

void VcsEditorDocument::setLogEntryPattern(const QString &pattern)
{
    regexpFromString(pattern, &d->logEntryPattern);
    updateSections();
}

QRegularExpression VcsEditorDocument::logEntryPattern() const
{
    return d->logEntryPattern;
}

void VcsEditorDocument::setAnnotationEntryPattern(const QString &pattern)
{
    regexpFromString(pattern, &d->annotation.entryPattern, QRegularExpression::MultilineOption);
}

void VcsEditorDocument::setAnnotationSeparatorPattern(const QString &pattern)
{
    regexpFromString(pattern, &d->annotation.separatorPattern);
}

Annotation VcsEditorDocument::annotation() const
{
    return d->annotation;
}

QSet<QString> VcsEditorDocument::annotationChanges() const
{
    QSet<QString> changes;
    const QString text = plainText();
    QStringView txt = QStringView(text);
    if (txt.isEmpty())
        return changes;
    if (!d->annotation.separatorPattern.pattern().isEmpty()) {
        const QRegularExpressionMatch match = d->annotation.separatorPattern.match(txt);
        if (match.hasMatch())
            txt.truncate(match.capturedStart());
    }
    QRegularExpressionMatchIterator i = d->annotation.entryPattern.globalMatch(txt);
    while (i.hasNext()) {
        const QRegularExpressionMatch match = i.next();
        changes.insert(match.captured(1));
    }
    return changes;
}

void VcsEditorDocument::setSectionHooks(const BlockToString &fileNameForDiffHeader,
                                        const BlockToString &revisionSubject)
{
    d->fileNameForDiffHeader = fileNameForDiffHeader;
    d->revisionSubject = revisionSubject;
    updateSections();
}

VcsEditorSections *VcsEditorDocument::sections() const
{
    return d->sections;
}

TextEditor::ToolBarChoice *VcsEditorDocument::toolBarChoice() const
{
    return d->choice;
}

QList<QAction *> VcsEditorDocument::contextMenuActions(const QTextCursor &cursor)
{
    // The previous click's are gone with its menu; these live until the next.
    qDeleteAll(d->contextActions);
    d->contextActions.clear();
    if (cursor.isNull())
        return {};

    const auto add = [this](const QString &text, const std::function<void()> &act) {
        auto * const action = new QAction(text, this);
        connect(action, &QAction::triggered, this, act);
        d->contextActions.append(action);
    };
    const EditorContentType type = d->parameters.type;

    // What is under the pointer, in the order the widget's handlers are asked.
    if (type == LogOutput || type == AnnotateOutput) {
        const QString change = d->parameters.changeUnderCursor
                                   ? d->parameters.changeUnderCursor(cursor) : QString();
        Internal::UrlUnderCursor url;
        Internal::UrlUnderCursor mail;
        if (change.isEmpty())
            url = Internal::urlUnderCursor(cursor);
        if (change.isEmpty() && !url.isValid())
            mail = Internal::emailUnderCursor(cursor);
        if (!change.isEmpty()) {
            add(Tr::tr("Copy \"%1\"").arg(change), [change] { Utils::setClipboardAndSelection(change); });
            add(Tr::tr("&Describe Change %1").arg(change), [this, change] {
                if (d->parameters.describeFunc)
                    d->parameters.describeFunc(VcsBase::source(this), change);
            });
            // An annotation offers the revision itself; a log offers the file
            // annotated at it, where the VCS has said its log can.
            if (type == AnnotateOutput || d->fileLogAnnotateEnabled) {
                const int line = cursor.blockNumber() + 1;
                add(d->annotateRevisionTextFormat.arg(change),
                    [this, change, line] { requestAnnotation(change, line); });
            }
        } else if (url.isValid()) {
            add(Tr::tr("Open URL in Browser..."),
                [address = url.url] { QDesktopServices::openUrl(QUrl(address)); });
            add(Tr::tr("Copy URL Location"),
                [address = url.url] { Utils::setClipboardAndSelection(address); });
        } else if (mail.isValid()) {
            add(Tr::tr("Send Email To..."), [address = mail.url] {
                QDesktopServices::openUrl(QUrl("mailto:" + address));
            });
            add(Tr::tr("Copy Email Address"),
                [address = mail.url] { Utils::setClipboardAndSelection(address); });
        }
    }

    // A log or a diff can be pasted, where there is somewhere to paste it to.
    if (type == LogOutput || type == DiffOutput) {
        if (auto * const paste = ExtensionSystem::PluginManager::getObject<CodePaster::Service>())
            add(Tr::tr("Send to CodePaster..."), [paste] { paste->postCurrentEditor(); });
    }
    return d->contextActions;
}

// What the widget's slotAnnotateRevision() works out: the working directory
// from this document or the VCS above the source, and the file relative to
// it. The source stands in for the widget's fileNameForLine(), which only Git
// answers differently.
void VcsEditorDocument::requestAnnotation(const QString &change, int line)
{
    const FilePath fileName = VcsBase::source(this).canonicalPath();
    const FilePath ownWorkingDirectory = d->workingDirectory;
    const FilePath workingDirectory = ownWorkingDirectory.isEmpty()
            ? Core::VcsManager::findTopLevelForDirectory(fileName.parentDir())
            : ownWorkingDirectory;
    const FilePath relativePath = fileName.isRelativePath()
            ? fileName
            : fileName.relativeChildPath(workingDirectory);
    emit annotateRevisionRequested(workingDirectory, relativePath.toUrlishString(), change, line);
}

void VcsEditorDocument::updateSections()
{
    QStringList entries;
    QList<int> lines;
    QTextDocument * const text = document();
    const QTextBlock end = text->end();
    int lineNumber = 0;
    switch (d->parameters.type) {
    case DiffOutput: {
        // One section per diffed file, headers skipped, a file repeated
        // under the last one counted once.
        QString lastFileName;
        for (QTextBlock it = text->begin(); it != end; it = it.next(), ++lineNumber) {
            if (d->diffFilePattern.match(it.text()).capturedStart() != 0)
                continue;
            const QString file = d->fileNameForDiffHeader ? d->fileNameForDiffHeader(it)
                                                          : QString();
            if (file.isEmpty() || lastFileName == file)
                continue;
            lastFileName = file;
            lines.push_back(lines.empty() ? 0 : lineNumber);
            entries.append(FilePath::fromString(file).fileName());
        }
        break;
    }
    case LogOutput:
        // One section per log entry, named by what the pattern captures and
        // the entry's subject if there is one.
        for (QTextBlock it = text->begin(); it != end; it = it.next(), ++lineNumber) {
            const QRegularExpressionMatch match = d->logEntryPattern.match(it.text());
            if (!match.hasMatch())
                continue;
            lines.push_back(lines.empty() ? 0 : lineNumber);
            QString entry = match.captured(1);
            QString subject = d->revisionSubject ? d->revisionSubject(it) : QString();
            if (!subject.isEmpty()) {
                if (subject.size() > 100) {
                    subject.truncate(97);
                    subject.append("...");
                }
                entry.append(" - ").append(subject);
            }
            entries.append(entry);
        }
        break;
    default:
        break;
    }
    d->sections->reset(entries, lines);
}

} // namespace VcsBase

#ifdef WITH_TESTS

#include "vcsbaseeditor.h"

#include <utils/algorithm.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

namespace VcsBase {

class VcsEditorDocumentTest final : public QObject
{
    Q_OBJECT

private slots:
    // What a VCS editor is configured with lives on its document, and the
    // widget editor's accessors are a view on it: set on either side, seen
    // on the other. What a subclass declares in its constructor, before there
    // is a document, reaches the document too. And the sections follow the
    // text, with the one thing the document cannot know - a log entry's
    // subject - asked of the widget that knows.
    void testTheWidgetEditorsStateIsTheDocuments()
    {
        class LogWidget final : public VcsBaseEditorWidget
        {
        public:
            LogWidget() { setLogEntryPattern("^entry ([0-9]+)"); }

        private:
            QString changeUnderCursor(const QTextCursor &) const final { return {}; }
            BaseAnnotationHighlighterCreator annotationHighlighterCreator() const final
            {
                return {};
            }
            QString revisionSubject(const QTextBlock &block) const final
            {
                return block.next().text().trimmed();
            }
        };
        const VcsBaseEditorParameters parameters{LogOutput,
                                                 "VcsEditorDocumentTest.Log",
                                                 "VCS document test log",
                                                 "text/vnd.qtcreator.vcs-document-test",
                                                 [] { return new LogWidget; },
                                                 [](const FilePath &, const QString &) {}};
        VcsEditorFactory factory(parameters);
        // Through the editor manager, as a client opens one: the choice below
        // jumps in the editor the manager knows for the document.
        QString title = "VCS document test";
        Core::IEditor * const editor
            = Core::EditorManager::openEditorWithContents(parameters.id, &title, QByteArray());
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}, false); });
        VcsBaseEditorWidget * const widget = VcsBaseEditor::getVcsBaseEditor(editor);
        QVERIFY2(widget, "the factory did not build a VCS editor");
        VcsEditorDocument * const document = widget->vcsDocument();
        QVERIFY2(document, "the VCS editor's document is not a VCS document");
        QCOMPARE(document->contentType(), LogOutput);
        QCOMPARE(widget->contentType(), LogOutput);

        widget->setWorkingDirectory(FilePath::fromString("/somewhere/checked/out"));
        QCOMPARE(document->workingDirectory(), FilePath::fromString("/somewhere/checked/out"));
        document->setFirstLineNumber(41);
        QCOMPARE(widget->firstLineNumber(), 41);
        widget->setDefaultLineNumber(7);
        QCOMPARE(document->defaultLineNumber(), 7);

        QVERIFY2(!document->logEntryPattern().pattern().isEmpty(),
                 "the pattern the constructor declared never reached the document");

        document->setPlainText("entry 1\n  first\nmore\nentry 2\n  second\n");
        QCOMPARE(document->sections()->entries(), QStringList({"1 - first", "2 - second"}));
        QCOMPARE(document->sections()->lines(), QList<int>({0, 3}));
        QCOMPARE(document->sections()->sectionOfLine(2), 0);
        QCOMPARE(document->sections()->sectionOfLine(4), 1);
        QCOMPARE(document->sections()->data(document->sections()->index(1), VcsEditorSections::LineRole).toInt(), 3);

        // And the sections are what the tool bar offers: the row the caret is
        // in is current, and choosing a row goes there. Not a pick to undo.
        TextEditor::ToolBarChoice * const choice = document->toolBarChoice();
        QVERIFY2(choice, "a log document offers the tool bar no choice");
        QCOMPARE(choice->model(), document->sections());
        QVERIFY2(choice->isAvailable(), "two sections, and nothing to choose between");
        QVERIFY2(!choice->isChosen(), "a jump is offered as a pick to undo");
        choice->followCaret(5);
        QCOMPARE(choice->currentIndex(), 1);
        choice->followCaret(2);
        QCOMPARE(choice->currentIndex(), 0);
        // Setting the text left the widget editor's caret at the end; what
        // matters is that a choice moves it, in both directions.
        QVERIFY2(editor->currentLine() != 4, "the caret starts where the jump would land");
        choice->choose(1);
        QVERIFY2(editor->currentLine() == 4,
                 qPrintable(QString("choosing the second entry left the caret on line %1")
                                .arg(editor->currentLine())));
        choice->choose(0);
        QVERIFY2(editor->currentLine() == 1,
                 qPrintable(QString("choosing the first entry left the caret on line %1")
                                .arg(editor->currentLine())));
    }

    // What a log holds that a plain click can do something with: a change,
    // a URL, a Jira key, an address - each offered over exactly itself, the
    // change described with the document's source. Nothing over anything
    // else, and nothing at all in a diff, where the widget editor's handlers
    // were never asked either.
    void testALogOffersWhatIsUnderThePointerToDoSomethingWith()
    {
        FilePath describedSource;
        QString describedChange;
        const VcsBaseEditorParameters parameters{
            LogOutput,
            "VcsEditorDocumentTest.Links",
            "VCS document test links",
            "text/vnd.qtcreator.vcs-document-links-test",
            {},
            [&describedSource, &describedChange](const FilePath &source, const QString &change) {
                describedSource = source;
                describedChange = change;
            },
            [](const QTextCursor &cursor) {
                static const QRegularExpression eightHex(
                    QRegularExpression::anchoredPattern("[0-9a-f]{8}"));
                QTextCursor word = cursor;
                word.select(QTextCursor::WordUnderCursor);
                const QString text = word.selectedText();
                return eightHex.match(text).hasMatch() ? text : QString();
            }};
        VcsEditorDocument document(parameters);
        VcsBase::setSource(&document, FilePath::fromString("/repo/file.cpp"));
        const QString text = "commit 1234abcd\n"
                             "see https://example.org/x?a=1 now\n"
                             "Task-number: QTCREATORBUG-123\n"
                             "mail me@example.com\n";
        document.setPlainText(text);

        const TextEditor::ActionLinkFinder finder
            = TextEditor::TextEditorFactory::actionLinkFinderFor(&document);
        QVERIFY2(finder, "a log offers nothing to do under the pointer");
        const auto at = [&document, &finder](int position) {
            QTextCursor cursor(document.document());
            cursor.setPosition(position);
            return finder(&document, cursor);
        };
        const auto spanOf = [&text](const QString &what) {
            const int start = int(text.indexOf(what));
            return std::make_pair(start, start + int(what.size()));
        };

        const auto change = spanOf("1234abcd");
        const TextEditor::ActionLink onChange = at(change.first + 3);
        QVERIFY2(onChange.isValid(), "the change is not offered");
        QCOMPARE(onChange.linkTextStart, change.first);
        QCOMPARE(onChange.linkTextEnd, change.second);
        onChange.activate();
        QCOMPARE(describedChange, QString("1234abcd"));
        QCOMPARE(describedSource, FilePath::fromString("/repo/file.cpp"));

        // Not activated: that opens a browser.
        for (const QString &what : {QString("https://example.org/x?a=1"),
                                    QString("QTCREATORBUG-123"),
                                    QString("me@example.com")}) {
            const auto span = spanOf(what);
            const TextEditor::ActionLink on = at(span.first + 2);
            QVERIFY2(on.isValid(), qPrintable(what + " is not offered"));
            QCOMPARE(on.linkTextStart, span.first);
            QCOMPARE(on.linkTextEnd, span.second);
        }
        QVERIFY2(!at(spanOf("commit").first + 1).isValid(), "a plain word is offered");
        QVERIFY2(!at(spanOf("now").first).isValid(), "the word after the URL is offered");

        VcsBaseEditorParameters diffParameters = parameters;
        diffParameters.type = DiffOutput;
        diffParameters.id = "VcsEditorDocumentTest.DiffLinks";
        VcsEditorDocument diff(diffParameters);
        QVERIFY2(!TextEditor::TextEditorFactory::actionLinkFinderFor(&diff),
                 "a diff offers something to do under the pointer");
    }

    // A right click in a log offers what the widget editor's handlers put in
    // its menu: for a change, to copy it, describe it and - where the VCS says
    // its log can - annotate the file at it; for a URL or an address, to open
    // and to copy. Each entry is tried, not only counted.
    void testALogsRightClickOffersWhatTheWidgetsHandlersDid()
    {
        FilePath describedSource;
        QString describedChange;
        const VcsBaseEditorParameters parameters{
            LogOutput,
            "VcsEditorDocumentTest.Menu",
            "VCS document test menu",
            "text/vnd.qtcreator.vcs-document-menu-test",
            {},
            [&describedSource, &describedChange](const FilePath &source, const QString &change) {
                describedSource = source;
                describedChange = change;
            },
            [](const QTextCursor &cursor) {
                static const QRegularExpression eightHex(
                    QRegularExpression::anchoredPattern("[0-9a-f]{8}"));
                QTextCursor word = cursor;
                word.select(QTextCursor::WordUnderCursor);
                const QString text = word.selectedText();
                return eightHex.match(text).hasMatch() ? text : QString();
            }};
        VcsEditorDocument document(parameters);
        VcsBase::setSource(&document, FilePath::fromString("/repo/sub/file.cpp"));
        document.setWorkingDirectory(FilePath::fromString("/repo"));
        const QString text = "commit 1234abcd\n"
                             "see https://example.org/x?a=1 now\n"
                             "Task-number: QTCREATORBUG-123\n"
                             "mail me@example.com\n";
        document.setPlainText(text);

        const auto cursorAt = [&document, &text](const QString &what) {
            QTextCursor cursor(document.document());
            cursor.setPosition(int(text.indexOf(what)) + 1);
            return cursor;
        };
        const auto textsOf = [](const QList<QAction *> &actions) {
            return Utils::transform(actions, &QAction::text);
        };
        const auto trigger = [](const QList<QAction *> &actions, const QString &text) {
            for (QAction * const action : actions) {
                if (action->text() == text) {
                    action->trigger();
                    return true;
                }
            }
            return false;
        };
        QClipboard * const clipboard = QGuiApplication::clipboard();

        // The change: copy, describe - and no annotation from a log that has
        // not said it can.
        QList<QAction *> onChange = document.contextMenuActions(cursorAt("1234abcd"));
        QStringList texts = textsOf(onChange);
        QCOMPARE(texts.mid(0, 2), QStringList({"Copy \"1234abcd\"", "&Describe Change 1234abcd"}));
        QVERIFY2(!texts.contains("Annotate \"1234abcd\""), "a log offered to annotate before it said it could");
        QVERIFY(trigger(onChange, "&Describe Change 1234abcd"));
        QCOMPARE(describedChange, QString("1234abcd"));
        QCOMPARE(describedSource, FilePath::fromString("/repo/sub/file.cpp"));
        clipboard->clear();
        QVERIFY(trigger(onChange, "Copy \"1234abcd\""));
        QCOMPARE(clipboard->text(), QString("1234abcd"));

        // Once it has: the annotation, asked for with the file relative to the
        // working directory and the line the click was on.
        document.setFileLogAnnotateEnabled(true);
        QSignalSpy annotated(&document, &VcsEditorDocument::annotateRevisionRequested);
        onChange = document.contextMenuActions(cursorAt("1234abcd"));
        QVERIFY(trigger(onChange, "Annotate \"1234abcd\""));
        QCOMPARE(annotated.count(), 1);
        QCOMPARE(annotated.first().at(0).value<FilePath>(), FilePath::fromString("/repo"));
        QCOMPARE(annotated.first().at(1).toString(), QString("sub/file.cpp"));
        QCOMPARE(annotated.first().at(2).toString(), QString("1234abcd"));
        QCOMPARE(annotated.first().at(3).toInt(), 1);

        // A URL and a Jira key: open and copy, the key copied as the URL it
        // stands for. An address: send and copy.
        QList<QAction *> onUrl = document.contextMenuActions(cursorAt("https://example.org"));
        QCOMPARE(textsOf(onUrl).mid(0, 2), QStringList({"Open URL in Browser...", "Copy URL Location"}));
        QVERIFY(trigger(onUrl, "Copy URL Location"));
        QCOMPARE(clipboard->text(), QString("https://example.org/x?a=1"));
        QList<QAction *> onKey = document.contextMenuActions(cursorAt("QTCREATORBUG-123"));
        QVERIFY(trigger(onKey, "Copy URL Location"));
        QCOMPARE(clipboard->text(), QString(Core::Constants::QT_JIRA_URL) + "/browse/QTCREATORBUG-123");
        QList<QAction *> onMail = document.contextMenuActions(cursorAt("me@example.com"));
        QCOMPARE(textsOf(onMail).mid(0, 2), QStringList({"Send Email To...", "Copy Email Address"}));
        QVERIFY(trigger(onMail, "Copy Email Address"));
        QCOMPARE(clipboard->text(), QString("me@example.com"));

        // Plain text: none of those.
        const QStringList onPlain = textsOf(document.contextMenuActions(cursorAt("commit")));
        for (const QString &entry : onPlain)
            QVERIFY2(!entry.startsWith("Copy") && !entry.startsWith("&Describe") && !entry.startsWith("Open"),
                     qPrintable("plain text offered: " + entry));

        // An annotation offers to annotate its revision without being told.
        VcsBaseEditorParameters annotateParameters = parameters;
        annotateParameters.type = AnnotateOutput;
        annotateParameters.id = "VcsEditorDocumentTest.AnnotateMenu";
        VcsEditorDocument annotation(annotateParameters);
        annotation.setPlainText("1234abcd (someone) line\n");
        QTextCursor onRevision(annotation.document());
        onRevision.setPosition(2);
        QVERIFY2(textsOf(annotation.contextMenuActions(onRevision)).contains("Annotate \"1234abcd\""),
                 "an annotation does not offer to annotate the revision under the pointer");
    }
};

QObject *createVcsEditorDocumentTest()
{
    return new VcsEditorDocumentTest;
}

} // namespace VcsBase

#endif // WITH_TESTS

#include "vcseditordocument.moc"

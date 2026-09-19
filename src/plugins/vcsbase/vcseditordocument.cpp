// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcseditordocument.h"

#include "vcsbaseeditorconfig.h"
#include "vcsbasetr.h"

#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>

#include <texteditor/texteditor.h>

#include <utils/qtcassert.h>

#include <QPointer>
#include <QTextBlock>
#include <QTextDocument>

using namespace Utils;

namespace VcsBase {

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

#include <QScopeGuard>
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
};

QObject *createVcsEditorDocumentTest()
{
    return new VcsEditorDocumentTest;
}

} // namespace VcsBase

#endif // WITH_TESTS

#include "vcseditordocument.moc"

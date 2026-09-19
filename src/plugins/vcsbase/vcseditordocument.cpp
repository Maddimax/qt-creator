// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcseditordocument.h"

#include "vcsbaseeditorconfig.h"
#include "vcsbasetr.h"

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
    // have nothing to find.
    if (parameters.type == LogOutput || parameters.type == DiffOutput)
        connect(this, &TextDocument::contentsChanged, this, &VcsEditorDocument::updateSections);
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

#include <coreplugin/editormanager/ieditor.h>

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
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY(editor);
        VcsBaseEditorWidget * const widget = VcsBaseEditor::getVcsBaseEditor(editor.get());
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
    }
};

QObject *createVcsEditorDocumentTest()
{
    return new VcsEditorDocumentTest;
}

} // namespace VcsBase

#endif // WITH_TESTS

#include "vcseditordocument.moc"

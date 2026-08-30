// Copyright (C) 2018 Andre Hartmann <aha_1980@gmx.de>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "filepropertiesdialog.h"

#include "../coreplugintr.h"
#include "ioptionspage.h"

#include <utils/aspects.h>
#include "../editormanager/ieditorfactory.h"
#include "../vcsmanager.h"

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/mimeutils.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QCheckBox>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>

using namespace Utils;

namespace Core {

// Everything the dialog shows about a file. Read-only strings rather than
// labels, so the values stay selectable the way the labels were, and check
// boxes for the three permissions that can be changed from here.
class FilePropertiesData final : public AspectContainer
{
public:
    FilePropertiesData()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/FilePropertiesDialog.qml"));

        const auto shown = [](StringAspect &aspect, const QString &qmlName,
                              const QString &label) {
            aspect.setQmlName(qmlName);
            aspect.setLabelText(label);
            aspect.setReadOnly(true);
        };
        shown(name, "Name", Tr::tr("Name:"));
        shown(path, "Path", Tr::tr("Path:"));
        shown(mimeType, "MimeType", Tr::tr("MIME type:"));
        shown(defaultEditor, "DefaultEditor", Tr::tr("Default editor:"));
        shown(lineEndings, "LineEndings", Tr::tr("Line endings:"));
        shown(indentation, "Indentation", Tr::tr("Indentation:"));
        shown(owner, "Owner", Tr::tr("Owner:"));
        shown(group, "Group", Tr::tr("Group:"));
        shown(size, "Size", Tr::tr("Size:"));
        shown(lastRead, "LastRead", Tr::tr("Last read:"));
        shown(lastModified, "LastModified", Tr::tr("Last modified:"));
        shown(vcsStatus, "VcsStatus", Tr::tr("Version control state:"));

        readable.setQmlName("Readable");
        readable.setLabelText(Tr::tr("Readable:"));
        writable.setQmlName("Writable");
        writable.setLabelText(Tr::tr("Writable:"));
        executable.setQmlName("Executable");
        executable.setLabelText(Tr::tr("Executable:", "adjective"));

        // Whether a file is a link is not something this dialog can change.
        symLink.setQmlName("SymLink");
        symLink.setLabelText(Tr::tr("Symbolic link:"));
        symLink.setEnabled(false);
    }

    StringAspect name{this};
    StringAspect path{this};
    StringAspect mimeType{this};
    StringAspect defaultEditor{this};
    StringAspect lineEndings{this};
    StringAspect indentation{this};
    StringAspect owner{this};
    StringAspect group{this};
    StringAspect size{this};
    StringAspect lastRead{this};
    StringAspect lastModified{this};
    StringAspect vcsStatus{this};
    BoolAspect readable{this};
    BoolAspect writable{this};
    BoolAspect executable{this};
    BoolAspect symLink{this};
};

class FilePropertiesDialog final : public QDialog
{
public:
    explicit FilePropertiesDialog(const FilePath &filePath);

private:
    void refresh();
    void setPermission(QFile::Permissions newPermissions, bool set);
    void detectTextFileSettings();

    FilePropertiesData m_data;
    const FilePath m_filePath;
};

FilePropertiesDialog::FilePropertiesDialog(const FilePath &filePath)
    : QDialog(dialogParent())
    , m_filePath(filePath)
{
    setWindowTitle(Tr::tr("File Properties"));
    resize(400, 395);

    const auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(createAspectForm(&m_data));
    layout->addWidget(buttonBox);

    // Ticking a permission writes it to the file and reads everything back:
    // changing one can change what the others report.
    m_data.readable.addOnChanged(this, [this] {
        setPermission(QFile::ReadUser | QFile::ReadOwner, m_data.readable());
    });
    m_data.writable.addOnChanged(this, [this] {
        setPermission(QFile::WriteUser | QFile::WriteOwner, m_data.writable());
    });
    m_data.executable.addOnChanged(this, [this] {
        setPermission(QFile::ExeUser | QFile::ExeOwner, m_data.executable());
    });

    refresh();
}

// What a text file's line endings and indentation look like, guessed from its
// first bytes. Both can come back Unknown: a file with no line breaks at all
// does not look like text, and one that is never indented says nothing about
// how it would be.
//
// Kept out of the dialog because it is a question about bytes, and there the
// only way to ask it was to open a file's properties and read two labels.
TextFileStyle guessTextFileStyle(const QByteArray &contents)
{
    TextFileStyle style;

    char lineSeparator = '\n';
    if (contents.contains("\r\n")) {
        style.lineEndings = TextFileStyle::LineEndings::Crlf;
    } else if (contents.contains("\n")) {
        style.lineEndings = TextFileStyle::LineEndings::Lf;
    } else if (contents.contains("\r")) {
        style.lineEndings = TextFileStyle::LineEndings::Cr;
        lineSeparator = '\r';
    } else {
        // That does not look like a text file at all, and then nothing can be
        // said about its indentation either.
        return style;
    }

    const auto leadingSpaces = [](const QByteArray &line) {
        for (int i = 0, max = line.size(); i < max; ++i) {
            if (line.at(i) != ' ')
                return i;
        }
        return 0;
    };

    bool tabIndented = false;
    int lastLineIndent = 0;
    std::map<int, int> indents;
    const QList<QByteArray> lines = contents.split(lineSeparator);
    for (const QByteArray &line : lines) {
        if (line.startsWith(' ')) {
            const int spaces = leadingSpaces(line);
            const int step = qAbs(spaces - lastLineIndent);
            // Ignore zero or one character indentation changes: those are
            // continuation lines being lined up, not an indent step.
            if (step < 2)
                continue;
            indents[step]++;
            lastLineIndent = spaces;
        } else if (line.startsWith('\t')) {
            tabIndented = true;
        }

        if (!indents.empty() && tabIndented)
            break;
    }

    const auto most = Utils::maxElementOrDefault(
        indents, [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
            return a.second < b.second;
        });

    if (!indents.empty()) {
        if (tabIndented) {
            style.indentation = TextFileStyle::Indentation::Mixed;
        } else {
            style.indentation = TextFileStyle::Indentation::Spaces;
            style.spaces = most.first;
        }
    } else if (tabIndented) {
        style.indentation = TextFileStyle::Indentation::Tabs;
    }

    return style;
}

QString lineEndingsText(TextFileStyle::LineEndings endings)
{
    switch (endings) {
    case TextFileStyle::LineEndings::Crlf: return Tr::tr("Windows (CRLF)");
    case TextFileStyle::LineEndings::Lf:   return Tr::tr("Unix (LF)");
    case TextFileStyle::LineEndings::Cr:   return Tr::tr("Mac (CR)");
    case TextFileStyle::LineEndings::Unknown: break;
    }
    return Tr::tr("Unknown");
}

QString indentationText(const TextFileStyle &style)
{
    switch (style.indentation) {
    case TextFileStyle::Indentation::Mixed:  return Tr::tr("Mixed");
    case TextFileStyle::Indentation::Tabs:   return Tr::tr("Tabs");
    case TextFileStyle::Indentation::Spaces: return Tr::tr("%1 Spaces").arg(style.spaces);
    case TextFileStyle::Indentation::Unknown: break;
    }
    return Tr::tr("Unknown");
}

void FilePropertiesDialog::detectTextFileSettings()
{
    const Result<QByteArray> contents = m_filePath.fileContents(/*maxsize*/ 50000);
    if (!contents) {
        m_data.lineEndings.setValue(Tr::tr("Unknown"));
        m_data.indentation.setValue(Tr::tr("Unknown"));
        return;
    }

    const TextFileStyle style = guessTextFileStyle(*contents);
    m_data.lineEndings.setValue(lineEndingsText(style.lineEndings));
    m_data.indentation.setValue(indentationText(style));
}

void FilePropertiesDialog::refresh()
{
    Utils::withNtfsPermissions<void>([this] {
        const QFileInfo fileInfo = m_filePath.toFileInfo();
        QLocale locale;

        m_data.name.setValue(m_filePath.fileName());
        m_data.path.setValue(m_filePath.parentDir().toUserOutput());

        const MimeType mimeType = Utils::mimeTypeForFile(m_filePath);
        m_data.mimeType.setValue(mimeType.name());

        const EditorFactories factories = IEditorFactory::preferredEditorTypes(m_filePath);
        m_data.defaultEditor.setValue(!factories.isEmpty() ? factories.at(0)->displayName()
                                                           : Tr::tr("Undefined"));

        m_data.owner.setValue(m_filePath.owner());
        m_data.group.setValue(m_filePath.group());
        m_data.size.setValue(locale.formattedDataSize(fileInfo.size()));
        m_data.readable.setValue(fileInfo.isReadable());
        m_data.writable.setValue(fileInfo.isWritable());
        m_data.executable.setValue(fileInfo.isExecutable());
        m_data.symLink.setValue(fileInfo.isSymLink());
        m_data.lastRead.setValue(fileInfo.lastRead().toString(locale.dateTimeFormat()));
        m_data.lastModified.setValue(fileInfo.lastModified().toString(locale.dateTimeFormat()));
        m_data.vcsStatus.setValue(VcsManager::fileStateText(VcsManager::fileState(m_filePath)));

        if (mimeType.inherits("text/plain")) {
            detectTextFileSettings();
        } else {
            m_data.lineEndings.setValue(Tr::tr("Unknown"));
            m_data.indentation.setValue(Tr::tr("Unknown"));
        }
    });
}

void FilePropertiesDialog::setPermission(QFile::Permissions newPermissions, bool set)
{
    Utils::withNtfsPermissions<void>([this, newPermissions, set] {
        QFile::Permissions permissions = m_filePath.permissions();
        if (set)
            permissions |= newPermissions;
        else
            permissions &= ~newPermissions;

        if (!m_filePath.setPermissions(permissions))
            qWarning() << "Cannot change permissions for file" << m_filePath;
    });

    refresh();
}

void executeFilePropertiesDialog(const FilePath &filePath)
{
    FilePropertiesDialog dialog(filePath);
    dialog.exec();
}

#ifdef WITH_TESTS

class FilePropertiesTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        FilePropertiesData data;
        const Utils::Result<> rendered
            = aspectFormRenders(&data, "FilePropertiesDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhichLineEndingsAFileUses()
    {
        using LineEndings = TextFileStyle::LineEndings;

        QCOMPARE(guessTextFileStyle("a\r\nb\r\n").lineEndings, LineEndings::Crlf);
        QCOMPARE(guessTextFileStyle("a\nb\n").lineEndings, LineEndings::Lf);
        QCOMPARE(guessTextFileStyle("a\rb\r").lineEndings, LineEndings::Cr);

        // A file with any CRLF in it is a Windows file, even where most of it
        // is not: this is asked of the first 50000 bytes, so a mixed file has
        // to answer something.
        QCOMPARE(guessTextFileStyle("a\nb\r\nc\n").lineEndings, LineEndings::Crlf);

        // No line break at all does not look like text, and then nothing is
        // said about indentation either - not even "no indentation".
        const TextFileStyle none = guessTextFileStyle("    not a line");
        QCOMPARE(none.lineEndings, LineEndings::Unknown);
        QCOMPARE(none.indentation, TextFileStyle::Indentation::Unknown);
        QCOMPARE(guessTextFileStyle({}).lineEndings, LineEndings::Unknown);
    }

    void testWhichIndentationAFileUses()
    {
        using Indentation = TextFileStyle::Indentation;

        // The step between one line's indent and the next, not the depth: a
        // file indented 0, 4, 8 is a four-space file.
        const TextFileStyle four = guessTextFileStyle("a\n    b\n        c\n");
        QCOMPARE(four.indentation, Indentation::Spaces);
        QCOMPARE(four.spaces, 4);

        const TextFileStyle two = guessTextFileStyle("a\n  b\n    c\n      d\n");
        QCOMPARE(two.indentation, Indentation::Spaces);
        QCOMPARE(two.spaces, 2);

        QCOMPARE(guessTextFileStyle("a\n\tb\n\t\tc\n").indentation, Indentation::Tabs);
        QCOMPARE(guessTextFileStyle("a\n    b\n\tc\n").indentation, Indentation::Mixed);

        // Never indented says nothing, rather than "zero spaces".
        QCOMPARE(guessTextFileStyle("a\nb\nc\n").indentation, Indentation::Unknown);

        // A one-character change is a continuation line being lined up, not an
        // indent step, so on its own it says nothing.
        QCOMPARE(guessTextFileStyle("a\n b\n").indentation, Indentation::Unknown);

        // But it is skipped rather than remembered: the line after it is still
        // measured from the last line that *was* an indent, so " b" then "  c"
        // is a two-space step and not a one-space one.
        const TextFileStyle lined = guessTextFileStyle("a\n b\n  c\n");
        QCOMPARE(lined.indentation, Indentation::Spaces);
        QCOMPARE(lined.spaces, 2);
    }
};

QObject *createFilePropertiesTest()
{
    return new FilePropertiesTest;
}

#endif // WITH_TESTS

} // Core

#ifdef WITH_TESTS
#include "filepropertiesdialog.moc"
#endif

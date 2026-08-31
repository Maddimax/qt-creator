// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "promptoverwritedialog.h"

#include "../coreplugintr.h"

#include "ioptionspage.h"

#include <utils/aspects.h>
#include <utils/stringutils.h>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QVBoxLayout>

using namespace Utils;

namespace Core {

// What a file is called once the folder they are all in has been said once.
// The widget version took the last (length - common - 1) characters, which
// drops the first character of the name when the files have no common folder
// at all - the length is then one too small.
QString relativeToCommon(const FilePath &filePath, const QString &nativeCommonPath)
{
    const QString native = filePath.toUserOutput();
    if (nativeCommonPath.isEmpty() || !native.startsWith(nativeCommonPath))
        return native;
    // The separator between the folder and the name belongs to neither.
    return native.mid(nativeCommonPath.size() + 1);
}

QString overwriteQuestion(const QString &nativeCommonPath)
{
    return Tr::tr("The following files already exist in the folder\n%1.\n"
                  "Would you like to overwrite them?").arg(nativeCommonPath);
}

namespace Internal {

// The files, and which of them the reader has left ticked.
class PromptOverwriteModel final : public QAbstractTableModel
{
public:
    void setFiles(const FilePaths &filePaths)
    {
        beginResetModel();
        m_rows.clear();
        const QString nativeCommonPath = filePaths.commonPath().toUserOutput();
        for (const FilePath &filePath : filePaths)
            m_rows.append({filePath, relativeToCommon(filePath, nativeCommonPath), true, true});
        endResetModel();
    }

    FilePaths files(bool checked) const
    {
        FilePaths result;
        for (const Row &row : m_rows) {
            if (row.checked == checked)
                result.push_back(row.filePath);
        }
        return result;
    }

    void setChecked(const FilePath &f, bool checked) { write(f, &Row::checked, checked); }
    void setEnabled(const FilePath &f, bool enabled) { write(f, &Row::enabled, enabled); }
    bool isChecked(const FilePath &f) const { return read(f, &Row::checked); }
    bool isEnabled(const FilePath &f) const { return read(f, &Row::enabled); }

    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : m_rows.size(); }
    int columnCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : 1; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Utils::AspectTable::CheckableRole)
            return true;
        if (role == Utils::AspectTable::EditableRole)
            return Utils::AspectTable::isWritable(flags(index));
        if (!index.isValid() || index.row() >= m_rows.size())
            return {};
        const Row &row = m_rows.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return row.display;
        case Qt::CheckStateRole:
            return row.checked ? Qt::Checked : Qt::Unchecked;
        default:
            return {};
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (role != Qt::CheckStateRole || !index.isValid() || index.row() >= m_rows.size())
            return false;
        m_rows[index.row()].checked = value.toInt() == Qt::Checked;
        emit dataChanged(index, index, {role});
        return true;
    }

    // One nameless column, so no heading.
    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid() || index.row() >= m_rows.size())
            return Qt::NoItemFlags;
        Qt::ItemFlags f = Qt::ItemIsUserCheckable;
        if (m_rows.at(index.row()).enabled)
            f |= Qt::ItemIsEnabled;
        return f;
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return Utils::AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

private:
    struct Row { FilePath filePath; QString display; bool checked = true; bool enabled = true; };

    int indexOf(const FilePath &f) const
    {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).filePath == f)
                return i;
        }
        return -1;
    }
    void write(const FilePath &f, bool Row::*member, bool value)
    {
        const int row = indexOf(f);
        if (row < 0)
            return;
        m_rows[row].*member = value;
        emit dataChanged(index(row, 0), index(row, 0));
    }
    bool read(const FilePath &f, bool Row::*member) const
    {
        const int row = indexOf(f);
        return row < 0 ? false : m_rows.at(row).*member;
    }

    QList<Row> m_rows;
};

class PromptOverwriteDialogPrivate final : public AspectContainer
{
public:
    PromptOverwriteDialogPrivate()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/PromptOverwriteDialog.qml"));

        question.setQmlName("Question");
        files.setQmlName("Files");
        files.setModel(&model);
    }

    TextDisplay question{this};
    TableAspect files{this};
    PromptOverwriteModel model;
};

} // namespace Internal

/*!
    \class Core::PromptOverwriteDialog
    \inmodule QtCreator
    \internal
    \brief The PromptOverwriteDialog class implements a dialog that asks
    users whether they want to overwrite files.

    The dialog displays the common folder and the files in a list where users
    can select the files to overwrite.
*/

PromptOverwriteDialog::PromptOverwriteDialog(QWidget *parent)
    : QDialog(parent)
    , d(new Internal::PromptOverwriteDialogPrivate)
{
    setWindowTitle(Tr::tr("Overwrite Existing Files"));
    setModal(true);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(createAspectForm(d.get()));
    mainLayout->addWidget(bb);
}

PromptOverwriteDialog::~PromptOverwriteDialog() = default;

void PromptOverwriteDialog::setFiles(const FilePaths &filePaths)
{
    d->model.setFiles(filePaths);
    d->question.setText(overwriteQuestion(filePaths.commonPath().toUserOutput()));
}

FilePaths PromptOverwriteDialog::files(Qt::CheckState cs) const
{
    return d->model.files(cs == Qt::Checked);
}

void PromptOverwriteDialog::setFileEnabled(const FilePath &f, bool e)
{
    d->model.setEnabled(f, e);
}

bool PromptOverwriteDialog::isFileEnabled(const FilePath &f) const
{
    return d->model.isEnabled(f);
}

void PromptOverwriteDialog::setFileChecked(const FilePath &f, bool e)
{
    d->model.setChecked(f, e);
}

bool PromptOverwriteDialog::isFileChecked(const FilePath &f) const
{
    return d->model.isChecked(f);
}

#ifdef WITH_TESTS

namespace Internal {

class PromptOverwriteDialogTest final : public QObject
{
    Q_OBJECT

private:
    static FilePaths twoFiles()
    {
        return {FilePath::fromString("/tmp/project/a.cpp"),
                FilePath::fromString("/tmp/project/b.cpp")};
    }

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        PromptOverwriteDialogPrivate settings;
        const Utils::Result<> rendered
            = aspectFormRenders(&settings, "PromptOverwriteDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheFolderIsSaidOnceAndTheRowsAreNames()
    {
        const QString common = FilePath::fromString("/tmp/project").toUserOutput();
        QCOMPARE(relativeToCommon(FilePath::fromString("/tmp/project/a.cpp"), common),
                 QString("a.cpp"));

        // Files with nothing in common keep their whole name. The widget
        // version took the last (length - 0 - 1) characters here, so the first
        // character of the path went missing.
        QCOMPARE(relativeToCommon(FilePath::fromString("/tmp/a.cpp"), QString()),
                 FilePath::fromString("/tmp/a.cpp").toUserOutput());
        QCOMPARE(relativeToCommon(FilePath::fromString("/elsewhere/a.cpp"), common),
                 FilePath::fromString("/elsewhere/a.cpp").toUserOutput());

        QVERIFY2(overwriteQuestion(common).contains(common),
                 "the question does not say which folder");
    }

    void testEveryFileStartsTicked()
    {
        // The dialog is asked when the files are already there, and the
        // answer it offers is "all of them".
        PromptOverwriteDialog dialog;
        dialog.setFiles(twoFiles());
        QCOMPARE(dialog.checkedFiles(), twoFiles());
        QVERIFY(dialog.uncheckedFiles().isEmpty());
    }

    void testWhatIsUntickedIsNotOverwritten()
    {
        PromptOverwriteDialog dialog;
        dialog.setFiles(twoFiles());
        dialog.setFileChecked(twoFiles().at(0), false);

        QCOMPARE(dialog.checkedFiles(), FilePaths{twoFiles().at(1)});
        QCOMPARE(dialog.uncheckedFiles(), FilePaths{twoFiles().at(0)});
        QVERIFY(!dialog.isFileChecked(twoFiles().at(0)));
        QVERIFY(dialog.isFileChecked(twoFiles().at(1)));
    }

    void testAFileTheCallerClosedCannotBeTicked()
    {
        // A caller that knows a file must not be written to turns its row off;
        // the row is still listed, so the reader can see it is there.
        PromptOverwriteDialog dialog;
        dialog.setFiles(twoFiles());
        QVERIFY(dialog.isFileEnabled(twoFiles().at(0)));
        dialog.setFileEnabled(twoFiles().at(0), false);
        QVERIFY(!dialog.isFileEnabled(twoFiles().at(0)));
        QVERIFY2(dialog.isFileEnabled(twoFiles().at(1)), "closing one row closed the other");
    }

    void testTheRowsAreCheckBoxes()
    {
        PromptOverwriteDialogPrivate settings;
        settings.model.setFiles(twoFiles());
        const QModelIndex first = settings.model.index(0, 0);

        // Answered, not merely truthy: an unanswered role reads as undefined
        // in QML, and a cell with no answer draws as a field.
        const QVariant checkable = settings.model.data(first, Utils::AspectTable::CheckableRole);
        QVERIFY2(checkable.isValid(), "the list was never told its rows are check boxes");
        QVERIFY(checkable.toBool());

        QVERIFY2(settings.model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty(),
                 "the nameless column came up with a heading");
        QVERIFY(settings.model.roleNames().values().contains("checkState"));
    }

    void testTickingACellReachesTheDialog()
    {
        PromptOverwriteDialogPrivate settings;
        settings.model.setFiles(twoFiles());
        QSignalSpy changed(&settings.model, &QAbstractItemModel::dataChanged);
        QVERIFY(settings.model.setData(settings.model.index(0, 0), Qt::Unchecked,
                                       Qt::CheckStateRole));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(settings.model.files(false), FilePaths{twoFiles().at(0)});
    }
};

QObject *createPromptOverwriteDialogTest()
{
    return new PromptOverwriteDialogTest;
}

} // namespace Internal

#endif // WITH_TESTS

} // Core

#ifdef WITH_TESTS
#include "promptoverwritedialog.moc"
#endif

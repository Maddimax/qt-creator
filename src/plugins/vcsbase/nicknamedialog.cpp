// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "nicknamedialog.h"

#include "vcsbasetr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/filepath.h>
#include <utils/stringutils.h>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#endif

#include <QDebug>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

using namespace Utils;

enum { NickNameRole = Qt::UserRole + 1 };

/*!
    \class VcsBase::Internal::NickNameDialog

    \brief The NickNameDialog class shows users from a mail cap file.

    Manages a list of users read from an extended
    mail cap file, consisting of 4 columns:  "Name Mail [AliasName [AliasMail]]".

    The names can be used for insertion into "RevBy:" fields; aliases will
    be preferred.
*/

namespace VcsBase::Internal {

// For code clarity, a struct representing the entries of a mail map file
// with parse and model functions.
class NickNameEntry
{
public:
    void clear();
    bool parse(const QString &);
    QString nickName() const;
    QList<QStandardItem *> toModelRow() const;
    static QString nickNameOf(const QStandardItem *item);

    QString name;
    QString email;
    QString aliasName;
    QString aliasEmail;
};

void NickNameEntry::clear()
{
    name.clear();
    email.clear();
    aliasName.clear();
    aliasEmail.clear();
}

// Parse "Hans Mustermann <HM@acme.de> [Alias [<alias@acme.de>]]"

bool NickNameEntry::parse(const QString &l)
{
    clear();
    const QChar lessThan = QLatin1Char('<');
    const QChar greaterThan = QLatin1Char('>');
    // Get first name/mail pair
    int mailPos = l.indexOf(lessThan);
    if (mailPos == -1)
        return false;
    name = l.mid(0, mailPos).trimmed();
    mailPos++;
    const int mailEndPos = l.indexOf(greaterThan, mailPos);
    if (mailEndPos == -1)
        return false;
    email = l.mid(mailPos, mailEndPos - mailPos);
    // get optional 2nd name/mail pair
    const int aliasNameStart = mailEndPos + 1;
    if (aliasNameStart >= l.size())
        return true;
    int aliasMailPos = l.indexOf(lessThan, aliasNameStart);
    if (aliasMailPos == -1) {
        aliasName =l.mid(aliasNameStart, l.size() -  aliasNameStart).trimmed();
        return true;
    }
    aliasName = l.mid(aliasNameStart, aliasMailPos - aliasNameStart).trimmed();
    aliasMailPos++;
    const int aliasMailEndPos = l.indexOf(greaterThan, aliasMailPos);
    if (aliasMailEndPos == -1)
        return true;
    aliasEmail = l.mid(aliasMailPos, aliasMailEndPos - aliasMailPos);
    return true;
}

// Format "Hans Mustermann <HM@acme.de>"
static inline QString formatNick(const QString &name, const QString &email)
{
    QString rc = name;
    if (!email.isEmpty()) {
        rc += QLatin1String(" <");
        rc += email;
        rc += QLatin1Char('>');
    }
    return rc;
}

QString NickNameEntry::nickName() const
{
    return aliasName.isEmpty() ? formatNick(name, email) : formatNick(aliasName, aliasEmail);
}

QList<QStandardItem *> NickNameEntry::toModelRow() const
{
    const QVariant nickNameData = nickName();
    const Qt::ItemFlags flags = Qt::ItemIsSelectable|Qt::ItemIsEnabled;
    auto i1 = new QStandardItem(name);
    i1->setFlags(flags);
    i1->setData(nickNameData, NickNameRole);
    auto i2 = new QStandardItem(email);
    i2->setFlags(flags);
    i2->setData(nickNameData, NickNameRole);
    auto i3 = new QStandardItem(aliasName);
    i3->setFlags(flags);
    i3->setData(nickNameData, NickNameRole);
    auto i4 = new QStandardItem(aliasEmail);
    i4->setFlags(flags);
    i4->setData(nickNameData, NickNameRole);
    QList<QStandardItem *> row;
    row << i1 << i2 << i3 << i4;
    return row;
}

QString NickNameEntry::nickNameOf(const QStandardItem *item)
{
    return item->data(NickNameRole).toString();
}

[[maybe_unused]] static QDebug operator<<(QDebug d, const NickNameEntry &e)
{
    d.nospace() << "Name='" << e.name  << "' Mail='" << e.email
            << " Alias='" << e.aliasName << " AliasEmail='" << e.aliasEmail << "'\n";
    return  d;
}

// A Quick table addresses roles by name, and asks the model whether a cell may
// be written to - a table whose cells say nothing is taken to be the reader's
// to edit, and a list of names read from a file is not.
class NickNameModel : public QStandardItemModel
{
public:
    using QStandardItemModel::QStandardItemModel;

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == AspectTable::EditableRole)
            return AspectTable::isWritable(flags(index));
        return QStandardItemModel::data(index, role);
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QStandardItemModel::roleNames());
    }
};

class NickNameSettings final : public AspectContainer
{
public:
    explicit NickNameSettings(QStandardItemModel *model)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/VcsBase/NickNameDialog.qml"));
        names.setQmlName("Names");
        names.setModel(model);
        names.setFilterPlaceholderText(Tr::tr("Filter"));
        // The names read best in the order they are written in.
        names.setSortColumn(0);
    }

    TableAspect names{this};
};

NickNameDialog::NickNameDialog(QStandardItemModel *model, QWidget *parent)
    : QDialog(parent)
    , m_model(model)
    , m_settings(new NickNameSettings(model))
{
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    okButton()->setEnabled(false);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(&m_settings->names, &TableAspect::chosenChanged, this, [this] {
        okButton()->setEnabled(m_settings->names.currentRow() >= 0);
    });
    // A row the reader picked and meant is the same as choosing it and
    // pressing Ok, which is what the widget view's activation did.
    connect(&m_settings->names, &TableAspect::rowActivated, this, [this] {
        if (okButton()->isEnabled())
            okButton()->click();
    });
}

NickNameDialog::~NickNameDialog() = default;

QPushButton *NickNameDialog::okButton() const
{
    return m_buttonBox->button(QDialogButtonBox::Ok);
}

QString NickNameDialog::nickName() const
{
    const int row = m_settings->names.currentRow();
    if (row < 0)
        return {};
    if (const QStandardItem *item = m_model->item(row, 0))
        return NickNameEntry::nickNameOf(item);
    return {};
}

QStandardItemModel *NickNameDialog::createModel(QObject *parent)
{
    auto model = new NickNameModel(parent);
    QStringList headers = {Tr::tr("Name"), Tr::tr("Email"), Tr::tr("Alias"), Tr::tr("Alias email")};
    model->setHorizontalHeaderLabels(headers);
    return model;
}

Result<> NickNameDialog::populateModelFromMailCapFile(const FilePath &fileName,
                                                      QStandardItemModel *model)
{
    if (const int rowCount = model->rowCount())
        model->removeRows(0, rowCount);
    if (fileName.isEmpty())
        return ResultOk;
    const Result<QByteArray> res = fileName.fileContents();
    if (!res)
         return ResultError(res.error());

    // Split into lines and read
    NickNameEntry entry;
    const QStringList lines = QString::fromUtf8(normalizeNewlines(*res)).trimmed().split('\n');
    const int count = lines.size();
    for (int i = 0; i < count; i++) {
        if (entry.parse(lines.at(i))) {
            model->appendRow(entry.toModelRow());
        } else {
            qWarning("%s: Invalid mail cap entry at line %d: '%s'\n",
                     qPrintable(fileName.toUserOutput()),
                     i + 1, qPrintable(lines.at(i)));
        }
    }
    model->sort(0);
    return ResultOk;
}

QStringList NickNameDialog::nickNameList(const QStandardItemModel *model)
{
    QStringList  rc;
    const int rowCount = model->rowCount();
    for (int r = 0; r < rowCount; r++)
        rc.push_back(NickNameEntry::nickNameOf(model->item(r, 0)));
    return rc;
}

#ifdef WITH_TESTS

// Two entries, one with an alias and one without, which is the difference the
// nick name is built from.
static const char mailMap[] =
    "Some Body <some.body@example.com>\n"
    "Other Person <other@example.com> Alias Name <alias@example.com>\n";

class NickNameDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        const std::unique_ptr<QStandardItemModel> model(NickNameDialog::createModel(nullptr));
        NickNameSettings settings(model.get());
        const Result<> rendered = Core::aspectFormRenders(&settings, "NickNameDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testNoColumnIsTheReadersToEdit()
    {
        // The list is read from a file; a Quick table takes cells that say
        // nothing about themselves to be editable, and would draw a field.
        const std::unique_ptr<QStandardItemModel> model(populated());
        QCOMPARE(model->rowCount(), 2);

        for (int row = 0; row < model->rowCount(); ++row) {
            for (int column = 0; column < model->columnCount(); ++column) {
                const QModelIndex index = model->index(row, column);
                QVERIFY2(!model->data(index, AspectTable::EditableRole).toBool(),
                         qPrintable(QString("row %1 column %2 could be edited")
                                        .arg(row).arg(column)));
            }
        }
    }

    void testWhatANameIsCalled()
    {
        // An alias is preferred where there is one, which is the point of the
        // extra two columns.
        const std::unique_ptr<QStandardItemModel> model(populated());
        const QStringList names = NickNameDialog::nickNameList(model.get());
        QCOMPARE(names.size(), 2);
        QVERIFY2(names.contains("Some Body <some.body@example.com>"),
                 qPrintable(names.join(", ")));
        QVERIFY2(names.contains("Alias Name <alias@example.com>"),
                 qPrintable(names.join(", ")));
    }

    void testPickingARowIsWhatTheDialogAnswers()
    {
        const std::unique_ptr<QStandardItemModel> model(populated());
        NickNameDialog dlg(model.get());

        // Nothing chosen is nothing to answer with, and nothing to accept.
        QVERIFY2(!dlg.okButton()->isEnabled(), "a name could be chosen without choosing one");
        QCOMPARE(dlg.nickName(), QString());

        dlg.m_settings->names.setCurrentRow(0);
        QVERIFY(dlg.okButton()->isEnabled());
        QCOMPARE(dlg.nickName(), NickNameEntry::nickNameOf(model->item(0, 0)));

        dlg.m_settings->names.setCurrentRow(1);
        QCOMPARE(dlg.nickName(), NickNameEntry::nickNameOf(model->item(1, 0)));
    }

    void testActivatingARowInTheDrawnTableAcceptsTheDialog()
    {
        // The .qml has to hand the activation back; a C++ test calling
        // activateRow() itself would not notice if that line went.
        const std::unique_ptr<QStandardItemModel> model(populated());
        NickNameDialog dlg(model.get());

        const std::unique_ptr<QWidget> form(Core::createAspectForm(dlg.m_settings.get()));
        QVERIFY(form);
        QObject *const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the dialog was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("nickNameTable"));

        QSignalSpy accepted(&dlg, &QDialog::accepted);
        QVERIFY(QMetaObject::invokeMethod(table, "rowActivated", Q_ARG(int, 1)));
        QTRY_COMPARE(accepted.count(), 1);
        QCOMPARE(dlg.nickName(), NickNameEntry::nickNameOf(model->item(1, 0)));
    }

private:
    // A model filled from a real mail map file, as the dialog is given one.
    static QStandardItemModel *populated()
    {
        auto *const model = NickNameDialog::createModel(nullptr);
        QTemporaryDir dir;
        if (!dir.isValid())
            return model;
        const FilePath file = FilePath::fromString(dir.filePath("mailmap"));
        if (!file.writeFileContents(QByteArray(mailMap)))
            return model;
        NickNameDialog::populateModelFromMailCapFile(file, model);
        return model;
    }
};

QObject *createNickNameDialogTest()
{
    return new NickNameDialogTest;
}

#endif // WITH_TESTS

} // VcsBase::Internal

#include "nicknamedialog.moc"

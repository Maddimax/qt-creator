// Copyright (C) 2022 The Qt Company Ltd
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "deletesymbolicnamedialog.h"

#include "squishtr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Utils;

namespace Squish::Internal {

// What the dialog explains before it asks. Rich text, because the name is
// wrapped so that it is not broken across lines.
QString deleteSymbolicNameDetails(const QString &nameToDelete)
{
    return Tr::tr("The Symbolic Name <span style='white-space: nowrap'>\"%1\"</span> you "
                  "want to remove is used in Multi Property Names. Select the action to "
                  "apply to references in these Multi Property Names.")
        .arg(nameToDelete);
}

// Whether the dialog can be accepted. Only the first of the three answers
// needs a name to point the references at; the other two need nothing.
bool canAccept(DeleteSymbolicNameDialog::Result result, bool hasSelection)
{
    return result != DeleteSymbolicNameDialog::ResetReference || hasSelection;
}

class SymbolicNamesModel final : public QAbstractTableModel
{
public:
    void setNames(const QStringList &names)
    {
        beginResetModel();
        m_names = names;
        endResetModel();
    }

    QString nameAt(int row) const
    { return row >= 0 && row < m_names.size() ? m_names.at(row) : QString(); }

    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : m_names.size(); }
    int columnCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : 1; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == AspectTable::EditableRole)
            return false;
        if (!index.isValid() || index.row() >= m_names.size() || role != Qt::DisplayRole)
            return {};
        return m_names.at(index.row());
    }

    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    { return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags; }

    QHash<int, QByteArray> roleNames() const override
    { return AspectTable::withRoleNames(QAbstractTableModel::roleNames()); }

private:
    QStringList m_names;
};

class DeleteSymbolicNameSettings final : public AspectContainer
{
public:
    DeleteSymbolicNameSettings(const QString &symbolicName, const QStringList &names)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Squish/DeleteSymbolicNameDialog.qml"));

        details.setQmlName("Details");
        details.setTextFormat(AspectControls::TextFormat::RichText);
        details.setWordWrap(true);
        details.setText(deleteSymbolicNameDetails(symbolicName));

        action.setQmlName("Action");
        action.setDisplayStyle(SelectionAspect::DisplayStyle::RadioButtons);
        action.addOption(
            Tr::tr("Adjust references to the removed symbolic name to point to:"));
        action.addOption(
            Tr::tr("Remove the symbolic name (invalidates names referencing it)"));
        action.addOption(Tr::tr("Remove the symbolic name and all names referencing it"));
        action.setValue(int(DeleteSymbolicNameDialog::ResetReference));

        names_.setQmlName("Names");
        names_.setModel(&model);
        names_.setFilterPlaceholderText(Tr::tr("Filter"));
        names_.setSortColumn(0);
        // Only the first answer points the references somewhere, so only then
        // is there a name to pick.
        connect(&action, &BaseAspect::changed, this, [this] {
            names_.setEnabled(result() == DeleteSymbolicNameDialog::ResetReference);
        });
        model.setNames(names);
    }

    DeleteSymbolicNameDialog::Result result() const
    {
        return DeleteSymbolicNameDialog::Result(action.volatileValue());
    }
    QString selectedName() const { return model.nameAt(names_.currentRow()); }
    bool canAccept() const
    {
        return Squish::Internal::canAccept(result(), names_.hasSelection());
    }

    TextDisplay details{this};
    SelectionAspect action{this};
    TableAspect names_{this};
    SymbolicNamesModel model;
};

DeleteSymbolicNameDialog::DeleteSymbolicNameDialog(const QString &symbolicName,
                                                   const QStringList &names,
                                                   QWidget *parent)
    : QDialog(parent)
    , d(new DeleteSymbolicNameSettings(symbolicName, names))
{
    m_buttonBox = new QDialogButtonBox;
    m_buttonBox->setOrientation(Qt::Horizontal);
    m_buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(d.get()));
    layout->addWidget(m_buttonBox);

    const auto updateOk = [this] {
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(d->canAccept());
    };
    connect(&d->action, &BaseAspect::changed, this, updateOk);
    connect(&d->names_, &TableAspect::chosenChanged, this, updateOk);
    updateOk();

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DeleteSymbolicNameDialog::~DeleteSymbolicNameDialog() = default;

QString DeleteSymbolicNameDialog::selectedSymbolicName() const
{
    return d->selectedName();
}

DeleteSymbolicNameDialog::Result DeleteSymbolicNameDialog::result() const
{
    return d->result();
}

#ifdef WITH_TESTS

class DeleteSymbolicNameDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        DeleteSymbolicNameSettings settings("aName", {"one", "two"});
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "DeleteSymbolicNameDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhenTheDialogCanBeAccepted()
    {
        // Only pointing the references somewhere needs a name to point at.
        QVERIFY(!canAccept(DeleteSymbolicNameDialog::ResetReference, false));
        QVERIFY(canAccept(DeleteSymbolicNameDialog::ResetReference, true));
        QVERIFY(canAccept(DeleteSymbolicNameDialog::InvalidateNames, false));
        QVERIFY(canAccept(DeleteSymbolicNameDialog::RemoveNames, false));
    }

    void testTheListIsOnlyThereToPointAt()
    {
        DeleteSymbolicNameSettings settings("aName", {"one", "two"});
        QVERIFY(settings.names_.isEnabled());

        settings.action.setValue(int(DeleteSymbolicNameDialog::RemoveNames));
        QVERIFY2(!settings.names_.isEnabled(),
                 "a name can be picked for an answer that points at nothing");

        settings.action.setValue(int(DeleteSymbolicNameDialog::ResetReference));
        QVERIFY(settings.names_.isEnabled());
    }

    void testTheThreeAnswersAreOffered()
    {
        DeleteSymbolicNameSettings settings("aName", {});
        QCOMPARE(settings.action.optionCount(), 3);
        // Pointing the references somewhere is the one the dialog opens on:
        // it is the answer that loses the least.
        QCOMPARE(settings.result(), DeleteSymbolicNameDialog::ResetReference);
    }

    void testTheDetailsNameWhatIsGoing()
    {
        const QString details = deleteSymbolicNameDetails("aName");
        QVERIFY2(details.contains("aName"), "the explanation does not say which name");
        QVERIFY2(details.contains("nowrap"), "the name may be broken across lines");

        DeleteSymbolicNameSettings settings("aName", {});
        QCOMPARE(settings.details.presentation().textFormat,
                 AspectControls::TextFormat::RichText);
    }

    void testTheNamesAreReadNotWritten()
    {
        SymbolicNamesModel model;
        model.setNames({"one", "two"});
        const QVariant editable = model.data(model.index(0, 0), AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(!editable.toBool(), "a symbolic name could be renamed here");
        QCOMPARE(model.nameAt(1), QString("two"));
        QVERIFY(model.nameAt(2).isEmpty());
        QVERIFY(model.nameAt(-1).isEmpty());
    }

    void testNothingPickedIsNoName()
    {
        const DeleteSymbolicNameDialog dialog("aName", {"one"}, nullptr);
        QVERIFY2(dialog.selectedSymbolicName().isEmpty(),
                 "the dialog names a symbolic name before one is picked");
    }
};

QObject *createDeleteSymbolicNameDialogTest()
{
    return new DeleteSymbolicNameDialogTest;
}

#endif // WITH_TESTS

} // namespace Squish::Internal

#ifdef WITH_TESTS
#include "deletesymbolicnamedialog.moc"
#endif

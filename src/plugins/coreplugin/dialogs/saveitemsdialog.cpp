// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "saveitemsdialog.h"

#include "../coreplugintr.h"
#include "../diffservice.h"
#include "../idocument.h"

#include "ioptionspage.h"

#include <utils/aspects.h>
#include <utils/fsengine/fileiconprovider.h>
#include <utils/hostosinfo.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Utils;

namespace Core::Internal {

// What the two buttons say depends on how much of the list is picked: all of
// it, some of it, or none - and none is nothing to do, so they are off.
QString saveButtonText(int selected, int total)
{
    if (selected == total && total > 0)
        return Tr::tr("&Save All");
    if (selected == 0)
        return Tr::tr("&Save");
    return Tr::tr("&Save Selected");
}

QString diffButtonText(int selected, int total)
{
    if (selected == total && total > 0)
        return Tr::tr("&Diff All && Cancel");
    if (selected == 0)
        return Tr::tr("&Diff && Cancel");
    return Tr::tr("&Diff Selected && Cancel");
}

// The name a document is listed under, and where it is. One that has never
// been saved has no path, so it is listed under the name it would be saved as.
QString documentDisplayName(const IDocument *document)
{
    const FilePath filePath = document->filePath();
    return filePath.isEmpty() ? document->fallbackSaveAsFileName() : filePath.fileName();
}

QString documentDirectory(const IDocument *document)
{
    const FilePath filePath = document->filePath();
    return filePath.isEmpty() ? QString() : filePath.absolutePath().toUserOutput();
}

class SaveItemsModel final : public QAbstractTableModel
{
public:
    explicit SaveItemsModel(const QList<IDocument *> &items)
        : m_documents(items.cbegin(), items.cend())
    {}

    IDocument *documentAt(int row) const
    {
        return row >= 0 && row < m_documents.size() ? m_documents.at(row).data() : nullptr;
    }

    // A document may have been closed - and deleted - elsewhere while this
    // modal dialog was open. A QPointer that has gone null is such a one.
    bool isStillOpen(const IDocument *document) const
    {
        return document && m_documents.contains(const_cast<IDocument *>(document));
    }

    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : m_documents.size(); }
    int columnCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : 2; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Utils::AspectTable::EditableRole)
            return false;
        const IDocument *document = documentAt(index.row());
        if (!document)
            return {};
        switch (role) {
        case Qt::DisplayRole:
            return index.column() == 0 ? documentDisplayName(document)
                                       : documentDirectory(document);
        case Qt::DecorationRole:
            if (index.column() == 0 && !document->filePath().isEmpty())
                return FileIconProvider::icon(document->filePath());
            return {};
        default:
            return {};
        }
    }

    // Two nameless columns: the widget tree hid its header.
    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return Utils::AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

private:
    QList<QPointer<IDocument>> m_documents;
};

class SaveItemsSettings final : public AspectContainer
{
public:
    explicit SaveItemsSettings(const QList<IDocument *> &items)
        : model(items)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/SaveItemsDialog.qml"));

        // Only where something is about to be lost. The widget label painted
        // itself with a theme colour; what it meant is that this is an error.
        warning.setQmlName("Warning");
        warning.setIconType(InfoType::Error);
        warning.setText(Tr::tr("Some files are externally modified, saving "
                               "them will discard the external changes!"));
        warning.setVisible(std::any_of(items.cbegin(), items.cend(), [](const IDocument *doc) {
            return doc->isConflicted();
        }));

        message.setQmlName("Message");
        message.setText(Tr::tr("The following files have unsaved changes:"));

        documents.setQmlName("Documents");
        documents.setModel(&model);

        // Named by the caller where there is one to name; not offered at all
        // otherwise.
        alwaysSave.setQmlName("AlwaysSave");
        alwaysSave.setVisible(false);
    }

    TextDisplay warning{this};
    TextDisplay message{this};
    TableAspect documents{this};
    BoolAspect alwaysSave{this};
    SaveItemsModel model;
};

SaveItemsDialog::SaveItemsDialog(QWidget *parent, const QList<IDocument *> &items)
    : QDialog(parent)
    , d(new SaveItemsSettings(items))
    , m_buttonBox(new QDialogButtonBox)
{
    resize(457, 200);
    setWindowTitle(Tr::tr("Save Changes"));

    // QDialogButtonBox's behavior for "destructive" is wrong, the "do not save" should be left-aligned
    const QDialogButtonBox::ButtonRole discardButtonRole = HostOsInfo::isMacHost()
                                                               ? QDialogButtonBox::ResetRole
                                                               : QDialogButtonBox::DestructiveRole;
    if (DiffService::instance()) {
        m_diffButton = m_buttonBox->addButton(Tr::tr("&Diff"), discardButtonRole);
        connect(m_diffButton, &QAbstractButton::clicked, this, &SaveItemsDialog::collectFilesToDiff);
    }
    m_buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Save);
    QPushButton *discardButton = m_buttonBox->addButton(Tr::tr("Do &Not Save"), discardButtonRole);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(createAspectForm(d.get()));
    layout->addWidget(m_buttonBox);

    // Everything, to begin with: the reader is being asked before something
    // closes, and saving all of it is the answer that loses nothing.
    QVariantList allRows;
    for (int row = 0; row < d->model.rowCount(); ++row)
        allRows.append(row);
    d->documents.setSelectedRows(allRows);

    connect(&d->documents, &TableAspect::chosenChanged, this, &SaveItemsDialog::updateButtons);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Save),
            &QAbstractButton::clicked,
            this,
            &SaveItemsDialog::collectItemsToSave);
    connect(discardButton, &QAbstractButton::clicked, this, &SaveItemsDialog::discardAll);

    adjustButtonWidths();
    updateButtons();
    m_buttonBox->button(QDialogButtonBox::Save)->setDefault(true);
}

SaveItemsDialog::~SaveItemsDialog() = default;

void SaveItemsDialog::setMessage(const QString &msg)
{
    d->message.setText(msg);
}

void SaveItemsDialog::updateButtons()
{
    const int selected = d->documents.selectedRows().size();
    const int total = d->model.rowCount();
    const bool buttonsEnabled = selected > 0;

    QPushButton *saveButton = m_buttonBox->button(QDialogButtonBox::Save);
    saveButton->setEnabled(buttonsEnabled);
    saveButton->setText(saveButtonText(selected, total));
    if (m_diffButton) {
        m_diffButton->setEnabled(buttonsEnabled);
        m_diffButton->setText(diffButtonText(selected, total));
    }
}

void SaveItemsDialog::adjustButtonWidths()
{
    // give save button a size that all texts fit in, so it doesn't get resized
    // Mac: make cancel + save button same size (work around dialog button box issue)
    QStringList possibleTexts;
    possibleTexts << Tr::tr("Save") << Tr::tr("Save All");
    if (d->model.rowCount() > 1)
        possibleTexts << Tr::tr("Save Selected");
    int maxTextWidth = 0;
    QPushButton *saveButton = m_buttonBox->button(QDialogButtonBox::Save);
    for (const QString &text : std::as_const(possibleTexts)) {
        saveButton->setText(text);
        int hint = saveButton->sizeHint().width();
        if (hint > maxTextWidth)
            maxTextWidth = hint;
    }
    if (HostOsInfo::isMacHost()) {
        QPushButton *cancelButton = m_buttonBox->button(QDialogButtonBox::Cancel);
        int cancelButtonWidth = cancelButton->sizeHint().width();
        if (cancelButtonWidth > maxTextWidth)
            maxTextWidth = cancelButtonWidth;
        cancelButton->setMinimumWidth(maxTextWidth);
    }
    saveButton->setMinimumWidth(maxTextWidth);
}

void SaveItemsDialog::collectItemsToSave()
{
    m_itemsToSave.clear();
    for (int row : d->documents.selectedRows()) {
        IDocument *document = d->model.documentAt(row);
        // document might have been closed (and deleted) elsewhere while this modal dialog was open
        if (d->model.isStillOpen(document))
            m_itemsToSave.append(document);
    }
    accept();
}

void SaveItemsDialog::collectFilesToDiff()
{
    m_filesToDiff.clear();
    for (int row : d->documents.selectedRows()) {
        IDocument *doc = d->model.documentAt(row);
        // document might have been closed (and deleted) elsewhere while this modal dialog was open
        if (d->model.isStillOpen(doc))
            m_filesToDiff.append(doc->filePath());
    }
    reject();
}

void SaveItemsDialog::discardAll()
{
    d->documents.setSelectedRows({});
    collectItemsToSave();
}

QList<IDocument*> SaveItemsDialog::itemsToSave() const
{
    return m_itemsToSave;
}

FilePaths SaveItemsDialog::filesToDiff() const
{
    return m_filesToDiff;
}

void SaveItemsDialog::setAlwaysSaveMessage(const QString &msg)
{
    d->alwaysSave.setLabelText(msg);
    d->alwaysSave.setVisible(true);
}

bool SaveItemsDialog::alwaysSaveChecked()
{
    return d->alwaysSave();
}

#ifdef WITH_TESTS

class SaveItemsDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        SaveItemsSettings settings({});
        const Utils::Result<> rendered
            = aspectFormRenders(&settings, "SaveItemsDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatTheButtonsSayFollowsTheSelection()
    {
        // All of it, some of it, or none - and none is nothing to do.
        QCOMPARE(saveButtonText(3, 3), Tr::tr("&Save All"));
        QCOMPARE(saveButtonText(1, 3), Tr::tr("&Save Selected"));
        QCOMPARE(saveButtonText(0, 3), Tr::tr("&Save"));

        QCOMPARE(diffButtonText(3, 3), Tr::tr("&Diff All && Cancel"));
        QCOMPARE(diffButtonText(1, 3), Tr::tr("&Diff Selected && Cancel"));
        QCOMPARE(diffButtonText(0, 3), Tr::tr("&Diff && Cancel"));

        // One document, picked, is all of them - not "Selected".
        QCOMPARE(saveButtonText(1, 1), Tr::tr("&Save All"));

        // An empty list is not "all of them" either, or an empty dialog would
        // offer to save everything.
        QCOMPARE(saveButtonText(0, 0), Tr::tr("&Save"));
    }

    void testTheWarningIsOnlyThereWhenSomethingWouldBeLost()
    {
        // No documents, nothing conflicted, so nothing to warn about.
        SaveItemsSettings settings({});
        QVERIFY2(!settings.warning.isVisible(),
                 "the dialog opens saying external changes will be discarded");
        QCOMPARE(settings.warning.presentation().infoType, InfoType::Error);
    }

    void testTheAlwaysSaveFlagIsNotOfferedUnnamed()
    {
        // The caller names it where there is one to name; the widget check box
        // was hidden until then and so is this.
        SaveItemsSettings settings({});
        QVERIFY2(!settings.alwaysSave.isVisible(),
                 "an unnamed check box is offered before the caller names it");

        SaveItemsDialog dialog(nullptr, {});
        QVERIFY(!dialog.alwaysSaveChecked());
        dialog.setAlwaysSaveMessage("Automatically save all files before building");
        QVERIFY(!dialog.alwaysSaveChecked());
    }

    void testTheRowsAreReadNotWritten()
    {
        SaveItemsSettings settings({});
        // Answered, not merely falsy: an unanswered role reads as undefined in
        // QML, and a cell with no answer is taken to be editable.
        const QVariant editable
            = settings.model.data(settings.model.index(0, 0), Utils::AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(!editable.toBool(), "a document could be renamed by typing in the list");

        QVERIFY2(settings.model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty(),
                 "the nameless columns came up with headings");
        QVERIFY(settings.model.roleNames().values().contains("display"));
    }

    void testADocumentClosedElsewhereIsNotSaved()
    {
        // The dialog is modal, but a document can still be closed and deleted
        // while it is open. A row whose document has gone is skipped.
        SaveItemsSettings settings({});
        QVERIFY2(!settings.model.isStillOpen(nullptr), "a document that is gone was saved anyway");
        QCOMPARE(settings.model.documentAt(0), nullptr);
        QCOMPARE(settings.model.documentAt(-1), nullptr);
    }

    void testNothingPickedSavesNothing()
    {
        // "Do Not Save" clears the selection and then collects, so what it
        // collects has to be empty.
        SaveItemsDialog dialog(nullptr, {});
        QVERIFY(dialog.itemsToSave().isEmpty());
        QVERIFY(dialog.filesToDiff().isEmpty());
    }
};

QObject *createSaveItemsDialogTest()
{
    return new SaveItemsDialogTest;
}

#endif // WITH_TESTS

} // Core::Internal

#ifdef WITH_TESTS
#include "saveitemsdialog.moc"
#endif

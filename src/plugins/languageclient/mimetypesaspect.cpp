// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "mimetypesaspect.h"

#include "languageclienttr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/algorithm.h>
#include <utils/aspectpresentation.h>
#include <utils/guiutils.h>
#include <utils/mimeutils.h>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

#include <QDialog>
#include <QDialogButtonBox>
#include <QStringListModel>
#include <QVBoxLayout>

namespace LanguageClient {

static constexpr char filterSeparator = ';';

class MimeTypeModel final : public QStringListModel
{
public:
    using QStringListModel::QStringListModel;

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (index.isValid() && role == Qt::CheckStateRole)
            return m_selectedMimeTypes.contains(index.data().toString()) ? Qt::Checked
                                                                         : Qt::Unchecked;
        // A Qt Quick view cannot read flags(), so the cell is asked instead.
        if (role == Utils::AspectTable::CheckableRole)
            return flags(index).testFlag(Qt::ItemIsUserCheckable);
        if (role == Utils::AspectTable::EditableRole)
            return Utils::AspectTable::isWritable(flags(index));
        return QStringListModel::data(index, role);
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) final
    {
        if (index.isValid() && role == Qt::CheckStateRole) {
            const QString mimeType = index.data().toString();
            if (value.toInt() == Qt::Checked) {
                if (!m_selectedMimeTypes.contains(mimeType))
                    m_selectedMimeTypes.append(mimeType);
            } else {
                m_selectedMimeTypes.removeAll(mimeType);
            }
            emit dataChanged(index, index, {role});
            return true;
        }
        return QStringListModel::setData(index, value, role);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const final
    {
        if (!index.isValid())
            return Qt::NoItemFlags;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    }

    // One nameless column: without this the heading reads "1", which the list
    // this replaces had no room to show at all.
    QVariant headerData(int, Qt::Orientation, int) const final { return {}; }

    QHash<int, QByteArray> roleNames() const final
    {
        return Utils::AspectTable::withRoleNames(QStringListModel::roleNames());
    }

    QStringList m_selectedMimeTypes;
};

class MimeTypeSelection final : public Utils::AspectContainer
{
    // First, so that it is destroyed last: the aspect does not own the model,
    // and hands it out for as long as a form is drawn from the container.
    MimeTypeModel m_model;

public:
    explicit MimeTypeSelection(const QStringList &selectedMimeTypes)
        : m_model(Utils::transform(Utils::allMimeTypes(), &Utils::MimeType::name))
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/LanguageClient/MimeTypeDialog.qml"));

        m_model.m_selectedMimeTypes = selectedMimeTypes;

        mimeTypes.setQmlName("MimeTypes");
        mimeTypes.setModel(&m_model);
        mimeTypes.setFilterPlaceholderText(Tr::tr("Filter"));
        mimeTypes.setSortColumn(0);
    }

    QStringList selectedMimeTypes() const { return m_model.m_selectedMimeTypes; }

    Utils::TableAspect mimeTypes{this};
};

class MimeTypeDialog : public QDialog
{
public:
    explicit MimeTypeDialog(const QStringList &selectedMimeTypes, QWidget *parent = nullptr)
        : QDialog(parent)
        , m_selection(selectedMimeTypes)
    {
        setWindowTitle(Tr::tr("Select MIME Types"));
        setModal(true);
        resize(400, 500);

        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

        auto mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(Core::createAspectForm(&m_selection));
        mainLayout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    MimeTypeDialog(const MimeTypeDialog &) = delete;
    MimeTypeDialog(MimeTypeDialog &&) = delete;
    MimeTypeDialog &operator=(const MimeTypeDialog &) = delete;
    MimeTypeDialog &operator=(MimeTypeDialog &&) = delete;

    QStringList mimeTypes() const { return m_selection.selectedMimeTypes(); }

private:
    MimeTypeSelection m_selection;
};

MimeTypesAspect::MimeTypesAspect(Utils::AspectContainer *container)
    : TypedAspect(container)
{
    setDefaultValue({});
    setLabelText(Tr::tr("Language:"));

    // TODO: remove once the lsp settings are fully aspectified
    connect(this, &Utils::BaseAspect::volatileValueChanged, this, &Utils::markSettingsDirty);
}

Utils::AspectPresentation MimeTypesAspect::presentation() const
{
    Utils::AspectPresentation p = TypedAspect::presentation();
    // A summary of what was picked, and the dialog that picks it. There are
    // several hundred MIME types, so a control that lists them in place is not
    // one of the options.
    p.control = Utils::AspectControls::TextWithAction;
    p.actionText = Tr::tr("Set MIME Types...");
    return p;
}

QString MimeTypesAspect::displayText() const
{
    return volatileValue().join(filterSeparator);
}

void MimeTypesAspect::triggerAction()
{
    MimeTypeDialog dialog(volatileValue(), Core::ICore::dialogParent());
    if (dialog.exec() == QDialog::Rejected)
        return;
    setVolatileValue(dialog.mimeTypes());
    emit displayTextChanged();
}

#ifdef WITH_TESTS

class MimeTypeDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        MimeTypeSelection selection({});
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&selection, "MimeTypeDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheListIsCheckBoxes()
    {
        MimeTypeModel model(QStringList{"text/plain", "text/x-c++src"});
        const QModelIndex first = model.index(0, 0);

        // Answered, not merely truthy: an unanswered role reads as undefined
        // in QML, and a cell with no answer draws as a field.
        const QVariant checkable = model.data(first, Utils::AspectTable::CheckableRole);
        QVERIFY2(checkable.isValid(), "the list was never told its rows are check boxes");
        QVERIFY2(checkable.toBool(), "a MIME type cannot be ticked");

        // The name itself is not the reader's to rewrite; only the tick is.
        const QVariant editable = model.data(first, Utils::AspectTable::EditableRole);
        QVERIFY(editable.isValid());
        QVERIFY2(editable.toBool(), "a ticked row is not writable");

        QVERIFY2(model.roleNames().values().contains("checkState"),
                 "the cell cannot read whether the row is ticked");
        QVERIFY2(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty(),
                 "the nameless column came up with a heading");
    }

    void testTickingATypePicksIt()
    {
        MimeTypeModel model(QStringList{"text/plain", "text/x-c++src"});
        model.m_selectedMimeTypes = {"text/x-c++src"};

        QCOMPARE(model.data(model.index(0, 0), Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
        QCOMPARE(model.data(model.index(1, 0), Qt::CheckStateRole).toInt(), int(Qt::Checked));

        QVERIFY(model.setData(model.index(0, 0), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(model.m_selectedMimeTypes, (QStringList{"text/x-c++src", "text/plain"}));

        // Ticking what is already ticked does not list it twice.
        QVERIFY(model.setData(model.index(0, 0), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(model.m_selectedMimeTypes.count("text/plain"), 1);

        QVERIFY(model.setData(model.index(1, 0), Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(model.m_selectedMimeTypes, (QStringList{"text/plain"}));
    }

    void testTheCellIsToldWhenTheTickChanges()
    {
        // A widget view read the check state back off flags() after any edit;
        // a Qt Quick cell binds to it and is only redrawn when told.
        MimeTypeModel model(QStringList{"text/plain"});
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QVERIFY(model.setData(model.index(0, 0), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(changed.count(), 1);
    }

    void testATypeThatIsGoneStaysPicked()
    {
        // The list holds every MIME type this installation knows. One that was
        // picked when it knew more cannot be unticked, so it has to survive
        // the dialog untouched.
        MimeTypeSelection selection({"text/x-vanished"});
        QVERIFY2(selection.selectedMimeTypes().contains("text/x-vanished"),
                 "a MIME type disappeared because nothing in the list stood for it");
    }
};

QObject *createMimeTypeDialogTest()
{
    return new MimeTypeDialogTest;
}

#endif // WITH_TESTS

} // namespace LanguageClient

#include "mimetypesaspect.moc"

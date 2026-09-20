// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pendingchangesdialog.h"

#include "perforcetr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#include <QAbstractListModel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace Perforce::Internal {

// One pending change, as p4 described it.
struct PendingChange
{
    int number = -1;
    QString description;
};

// What p4 printed, read into rows. Parsing here rather than while building a
// list means the dialog can be asked what it found without being shown.
static QList<PendingChange> parsePendingChanges(const QString &data)
{
    // Greedy on the digits. The expression this was ported from asked for
    // \\d+? and so captured a single one: every pending change was listed as
    // "Change 1", and submitting one ran p4 submit -c 1. The rest of the
    // expression is unchanged.
    static const QRegularExpression re("Change\\s(\\d+).*?\\s\\*?pending\\*?\\s(.+?)\n");
    QList<PendingChange> changes;
    QRegularExpressionMatchIterator it = re.globalMatch(data);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const int number = match.captured(1).trimmed().toInt(&ok);
        if (ok)
            changes.append({number, match.captured(2).trimmed()});
    }
    return changes;
}

class PendingChangesModel final : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;

    void setChanges(const QList<PendingChange> &changes)
    {
        beginResetModel();
        m_changes = changes;
        endResetModel();
    }

    int numberAt(int row) const
    {
        return row >= 0 && row < m_changes.size() ? m_changes.at(row).number : -1;
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_changes.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_changes.size() || role != Qt::DisplayRole)
            return {};
        const PendingChange &change = m_changes.at(index.row());
        return Tr::tr("Change %1: %2").arg(change.number).arg(change.description);
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0)
            return Tr::tr("Pending Changes");
        return QAbstractListModel::headerData(section, orientation, role);
    }

    QHash<int, QByteArray> roleNames() const override { return {{Qt::DisplayRole, "display"}}; }

private:
    QList<PendingChange> m_changes;
};

// The rows, and which of them is current. Not a TypedAspect: the value the
// dialog wants is a change number, and what the form edits is a selection.
class PendingChangesAspect final : public BaseAspect
{
    Q_OBJECT

    // Which row the form is showing as current. Written by the form; the
    // dialog reads the change number it stands for.
    Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)

public:
    explicit PendingChangesAspect(AspectContainer *container)
        : BaseAspect(container)
        // Parented: a model handed to QML from a property is one QML must not
        // take ownership of.
        , m_model(new PendingChangesModel(this))
    {
        setQmlName("Changes");
    }

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

    void setChanges(const QList<PendingChange> &changes)
    {
        m_model->setChanges(changes);
        // The first one, where there is one: the dialog used to do this so that
        // Submit could be enabled without the user picking anything first.
        setCurrentRow(changes.isEmpty() ? -1 : 0);
    }

    int currentRow() const { return m_currentRow; }

    void setCurrentRow(int row)
    {
        if (row == m_currentRow)
            return;
        m_currentRow = row;
        emit currentRowChanged();
    }

    int currentNumber() const { return m_model->numberAt(m_currentRow); }
    int count() const { return m_model->rowCount(); }

signals:
    void currentRowChanged();

private:
    PendingChangesModel *m_model = nullptr;
    int m_currentRow = -1;
};

class PendingChangesSettings final : public AspectContainer
{
public:
    PendingChangesSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Perforce/PendingChangesDialog.qml"));
    }

    PendingChangesAspect changes{this};
};

PendingChangesDialog::PendingChangesDialog(const QString &data, QWidget *parent)
    : QDialog(parent)
    , m_settings(new PendingChangesSettings)
{
    setWindowTitle(Tr::tr("P4 Pending Changes"));
    m_settings->changes.setChanges(parsePendingChanges(data));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *submitButton = buttonBox->addButton(Tr::tr("Submit"),
                                                     QDialogButtonBox::AcceptRole);
    submitButton->setEnabled(m_settings->changes.count() > 0);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(320, 250);
}

PendingChangesDialog::~PendingChangesDialog() = default;

int PendingChangesDialog::changeNumber() const
{
    return m_settings->changes.currentNumber();
}

#ifdef WITH_TESTS

class PendingChangesDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testItReadsWhatP4PrintedAndOffersIt()
    {
        PendingChangesSettings settings;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "PendingChangesDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        // What p4 prints, as it prints it. The parsing used to happen while
        // building list items, so nothing could ask what it found.
        const QString data =
            "Change 12345 on 2013/01/28 by User@Workspace *pending* 'First change'\n"
            "Change 12346 on 2013/01/29 by User@Workspace *pending* 'Second change'\n";
        settings.changes.setChanges(parsePendingChanges(data));
        QCOMPARE(settings.changes.count(), 2);

        // The first is current, which is what let Submit be enabled before the
        // user picked anything.
        QCOMPARE(settings.changes.currentRow(), 0);
        QCOMPARE(settings.changes.currentNumber(), 12345);

        settings.changes.setCurrentRow(1);
        QCOMPARE(settings.changes.currentNumber(), 12346);

        // Nothing pending is not row zero: the caller submits the number it is
        // given, and -1 is what says there is nothing to submit.
        PendingChangesDialog empty("", nullptr);
        QCOMPARE(empty.changeNumber(), -1);
    }
};

QObject *createPendingChangesDialogTest()
{
    return new PendingChangesDialogTest;
}

#endif // WITH_TESTS

} // Perforce::Internal

// Not guarded: a Q_OBJECT class of this file is production code, and
// without this its meta-object and vtable are missing in a build with
// tests off.
#include "pendingchangesdialog.moc"

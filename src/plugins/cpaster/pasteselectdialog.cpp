// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pasteselectdialog.h"

#include "cpastertr.h"
#include "protocol.h"
#include "settings.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/qtcassert.h>

#include <QtTaskTree/QSingleTaskTreeRunner>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

#include <QDialog>
#include <QDialogButtonBox>
#include <QStringListModel>
#include <QVBoxLayout>

#include <memory>

using namespace QtTaskTree;
using namespace Utils;

namespace CodePaster {

// What a listed entry stands for. The protocols return a line per paste -
// the id, then a title and a date lined up behind it - and only the first
// word of it is the paste itself.
QString pasteIdFromEntry(const QString &entry)
{
    QString id = entry;
    const int blankPos = id.indexOf(QLatin1Char(' '));
    if (blankPos != -1)
        id.truncate(blankPos);
    return id;
}

class PasteListModel final : public QStringListModel
{
public:
    using QStringListModel::QStringListModel;

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (role == AspectTable::EditableRole)
            return AspectTable::isWritable(flags(index));
        return QStringListModel::data(index, role);
    }

    // A listing is read, not written; the widget list these rows replace was
    // not editable either.
    Qt::ItemFlags flags(const QModelIndex &index) const final
    {
        if (!index.isValid())
            return Qt::NoItemFlags;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }

    // One nameless column, so no heading.
    QVariant headerData(int, Qt::Orientation, int) const final { return {}; }

    QHash<int, QByteArray> roleNames() const final
    {
        return AspectTable::withRoleNames(QStringListModel::roleNames());
    }
};

class PasteSelectSettings final : public AspectContainer
{
    // First, so that it is destroyed last: the aspect does not own the model.
    PasteListModel m_model;

public:
    explicit PasteSelectSettings(const QStringList &protocolNames)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CodePaster/PasteSelectDialog.qml"));

        protocol.setQmlName("Protocol");
        protocol.setLabelText(Tr::tr("Protocol:"));
        protocol.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        for (const QString &name : protocolNames)
            protocol.addOption(name);

        paste.setQmlName("Paste");
        paste.setLabelText(Tr::tr("Paste:"));
        paste.setDisplayStyle(StringAspect::LineEditDisplay);

        pastes.setQmlName("Pastes");
        pastes.setModel(&m_model);
        // The entries are columns lined up with spaces, which they only are in
        // a fixed-width font.
        pastes.setMonospace(true);

        refresh.setQmlName("Refresh");
        refresh.setActionText(Tr::tr("Refresh"));

        // Moving through the list fills the field, the way picking a row in
        // the list this replaces did. The whole entry, not the id: what is
        // shown is what the list shows, and the id is taken from it on accept.
        connect(&pastes, &TableAspect::chosenChanged, this, [this] {
            const QString chosen = entryAt(pastes.currentRow());
            if (!chosen.isEmpty())
                paste.setValue(chosen);
        });
    }

    void setEntries(const QStringList &entries) { m_model.setStringList(entries); }
    QString entryAt(int row) const
    {
        if (row < 0 || row >= m_model.rowCount())
            return {};
        return m_model.index(row, 0).data().toString();
    }

    QString pasteId() const { return pasteIdFromEntry(paste()); }

    SelectionAspect protocol{this};
    StringAspect paste{this};
    TableAspect pastes{this};
    ActionAspect refresh{this};
};

class PasteSelectDialog : public QDialog
{
public:
    explicit PasteSelectDialog(const QList<Protocol *> &protocols);

    QString pasteId() const { return m_settings.pasteId(); }
    int protocol() const { return m_settings.protocol(); }

private:
    void protocolChanged(int);
    void list();

    const QList<Protocol *> m_protocols;

    PasteSelectSettings m_settings;
    QSingleTaskTreeRunner m_taskTreeRunner;
};

PasteSelectDialog::PasteSelectDialog(const QList<Protocol *> &protocols)
    : QDialog(Core::ICore::dialogParent())
    , m_protocols(protocols)
    , m_settings(Utils::transform(protocols, [](const Protocol *p) { return p->name(); }))
{
    setObjectName("CodePaster.PasteSelectDialog");
    resize(550, 350);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(&m_settings));
    layout->addWidget(buttons);

    connect(&m_settings.pastes, &TableAspect::rowActivated, this, &QDialog::accept);
    connect(&m_settings.protocol, &BaseAspect::changed, this, [this] {
        protocolChanged(m_settings.protocol());
    });
    m_settings.refresh.setAction([this] { list(); });

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const int index = m_settings.protocol.indexForDisplay(settings().protocols.stringValue());
    if (index >= 0) {
        if (index != m_settings.protocol())
            m_settings.protocol.setValue(index);
        else
            protocolChanged(index); // Trigger a refresh
    }
}

void PasteSelectDialog::list()
{
    const int index = protocol();

    Protocol *protocol = m_protocols[index];
    QTC_ASSERT((protocol->capabilities() & Capability::List), return);

    m_settings.setEntries({});
    if (Protocol::ensureConfiguration(protocol)) {
        m_settings.setEntries({Tr::tr("Waiting for items...")});

        const auto listHandler = [this](const QStringList &results) {
            m_settings.setEntries(results);
        };
        const auto errorHandler = [this] {
            m_settings.setEntries({Tr::tr("Error while retrieving items.")});
        };
        m_taskTreeRunner.start({protocol->listRecipe(listHandler)}, {},
                               errorHandler, CallDoneFlag::OnError);
    }
}

void PasteSelectDialog::protocolChanged(int i)
{
    m_taskTreeRunner.reset();
    const bool canList = m_protocols.at(i)->capabilities() & Capability::List;
    m_settings.refresh.setEnabled(canList);
    if (canList)
        list();
    else
        m_settings.setEntries({Tr::tr("This protocol does not support listing")});
}

QString executeFetchDialog(const QList<Protocol *> &protocols)
{
    PasteSelectDialog dialog(protocols);

    if (dialog.exec() != QDialog::Accepted)
        return {};
    // Save new protocol in case user changed it.
    if (settings().protocols() != dialog.protocol()) {
        settings().protocols.setValue(dialog.protocol());
        settings().writeSettings();
    }
    return dialog.pasteId();
}

#ifdef WITH_TESTS

class PasteSelectDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        PasteSelectSettings settings({"Pastebin.Com", "Pastebin.Ca"});
        const Result<> rendered = Core::aspectFormRenders(&settings, "PasteSelectDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhichPasteAnEntryStandsFor()
    {
        // The listed line is the id and then what it is; only the first word
        // is asked for.
        QCOMPARE(pasteIdFromEntry("1234567 Some title 2026-08-31"), QString("1234567"));
        QCOMPARE(pasteIdFromEntry("1234567"), QString("1234567"));
        QCOMPARE(pasteIdFromEntry(""), QString());

        // What the reader typed themselves is a paste id as it stands.
        QCOMPARE(pasteIdFromEntry("abcdef"), QString("abcdef"));
    }

    void testTheListIsReadOnlyAndUnheaded()
    {
        PasteListModel model(QStringList{"1234567 Some title"});
        const QModelIndex first = model.index(0, 0);

        const QVariant editable = model.data(first, AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the list was never told whether a row may be written to");
        QVERIFY2(!editable.toBool(), "a listing could be typed over");

        QVERIFY2(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty(),
                 "the nameless column came up with a heading");
        QVERIFY(model.roleNames().values().contains("display"));
    }

    void testTheEntriesAreReadInColumns()
    {
        // The protocols line their listings up with spaces, so the rows are
        // only columns in a fixed-width font. The widget list set one on
        // itself; here the aspect says so and the cell follows.
        PasteSelectSettings settings({"Pastebin.Com"});
        QVERIFY2(settings.pastes.presentation().monospace,
                 "the listing is drawn in a proportional font and does not line up");
    }

    void testMovingThroughTheListFillsTheField()
    {
        PasteSelectSettings settings({"Pastebin.Com"});
        settings.setEntries({"1234567 First paste", "7654321 Second paste"});

        settings.pastes.setCurrentRow(1);
        QCOMPARE(settings.paste(), QString("7654321 Second paste"));
        // What is fetched is the id, not the line it was read off.
        QCOMPARE(settings.pasteId(), QString("7654321"));

        // A row that is no longer there leaves what was typed alone - the
        // field is the reader's as well as the list's.
        settings.paste.setValue("typed-by-hand");
        settings.pastes.setCurrentRow(-1);
        QCOMPARE(settings.paste(), QString("typed-by-hand"));
    }

    void testTheProtocolsAreOffered()
    {
        PasteSelectSettings settings({"Pastebin.Com", "DPaste.Com"});
        QCOMPARE(settings.protocol.indexForDisplay("DPaste.Com"), 1);
        QCOMPARE(settings.protocol.indexForDisplay("Nothing.Com"), -1);
    }

    void testTheDrawnTableHandsBackWhatWasChosen()
    {
        PasteSelectSettings settings({"Pastebin.Com"});
        settings.setEntries({"1234567 First paste", "7654321 Second paste"});

        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject *const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the dialog was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("pasteTable"));

        QSignalSpy activated(&settings.pastes, &TableAspect::rowActivated);
        QVERIFY(QMetaObject::invokeMethod(table, "selectRow", Q_ARG(int, 1)));
        QTRY_COMPARE(settings.paste(), QString("7654321 Second paste"));

        QVERIFY(QMetaObject::invokeMethod(table, "rowActivated", Q_ARG(int, 1)));
        QTRY_COMPARE(activated.count(), 1);
    }
};

QObject *createPasteSelectDialogTest()
{
    return new PasteSelectDialogTest;
}

#endif // WITH_TESTS

} // CodePaster

#include "pasteselectdialog.moc"

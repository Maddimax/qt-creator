// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "compilerexploreraspects.h"
#include "compilerexplorertr.h"

#include "api/library.h"

#include <utils/guiutils.h>

#ifdef WITH_TESTS
#include <QTest>
#endif
#include <utils/qtcassert.h>
#include <utils/layoutbuilder.h>
#include <utils/store.h>

#include <QComboBox>
#include <QCompleter>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>

using namespace Utils;

namespace CompilerExplorer {

LibrarySelectionAspect::LibrarySelectionAspect(AspectContainer *container)
    : TypedAspect<QMap<QString, QString>>(container)
{}

void LibrarySelectionAspect::volatileValueToGui()
{
    if (!m_model)
        return;

    for (int i = 0; i < m_model->rowCount(); i++) {
        QModelIndex idx = m_model->index(i, 0);
        const QString libId = idx.data(LibraryData).value<Api::Library>().id;
        if (m_volatileValue.contains(libId))
            m_model->setData(idx, m_volatileValue[libId], SelectedVersion);
        else
            m_model->setData(idx, QVariant(), SelectedVersion);
    }

    handleGuiChanged();
}

bool LibrarySelectionAspect::guiToVolatileValue()
{
    if (!m_model)
        return false;

    auto oldBuffer = m_volatileValue;

    m_volatileValue.clear();

    for (int i = 0; i < m_model->rowCount(); i++) {
        if (m_model->item(i)->data(SelectedVersion).isValid()) {
            m_volatileValue.insert(qvariant_cast<Api::Library>(m_model->item(i)->data(LibraryData)).id,
                            m_model->item(i)->data(SelectedVersion).toString());
        }
    }
    return oldBuffer != m_volatileValue;
}

static QVariantMap toVariantMap(const QMap<QString, QString> &map)
{
    QVariantMap variant;
    for (auto it = map.begin(); it != map.end(); ++it)
        variant.insert(it.key(), *it);
    return variant;
}

QVariant LibrarySelectionAspect::variantValue() const
{
    return toVariantMap(m_value);
}

QVariant LibrarySelectionAspect::volatileVariantValue() const
{
    return toVariantMap(m_volatileValue);
}

QVariant LibrarySelectionAspect::defaultVariantValue() const
{
    return toVariantMap(defaultValue());
}

void LibrarySelectionAspect::setVariantValue(const QVariant &value, Announcement howToAnnounce)
{
    QMap<QString, QString> map;
    const Store store = storeFromVariant(value);
    for (auto it = store.begin(); it != store.end(); ++it)
        map[stringFromKey(it.key())] = it->toString();
    setValue(map, howToAnnounce);
}

AspectPresentation LibrarySelectionAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::TextWithAction;
    p.actionText = Tr::tr("Edit");
    return p;
}

QString LibrarySelectionAspect::displayText() const
{
    if (!m_model)
        return Tr::tr("No libraries selected");

    QStringList libs;
    for (int i = 0; i < m_model->rowCount(); i++) {
        const QModelIndex idx = m_model->index(i, 0);
        if (!idx.data(LibraryData).isValid() || !idx.data(SelectedVersion).isValid())
            continue;
        const auto libData = idx.data(LibraryData).value<Api::Library>();
        const QString id = idx.data(SelectedVersion).toString();
        const auto versionIt = std::find_if(libData.versions.begin(),
                                            libData.versions.end(),
                                            [id](const Api::Library::Version &v) {
                                                return v.id == id;
                                            });
        libs.append(QString("%1 %2").arg(libData.name,
                                         versionIt == libData.versions.end() ? id
                                                                             : versionIt->version));
    }
    return libs.isEmpty() ? Tr::tr("No libraries selected") : libs.join(", ");
}

void LibrarySelectionAspect::ensureFilled()
{
    if (m_model)
        return;
    QTC_ASSERT(m_fillCallback, return);

    m_model = new QStandardItemModel(this);
    const auto cb = [this](const QList<QStandardItem *> &items) {
        for (QStandardItem *item : items)
            m_model->appendRow(item);
        volatileValueToGui();
        emit displayTextChanged();
    };
    connect(this, &LibrarySelectionAspect::refillRequested, this, [this, cb] {
        m_model->clear();
        m_fillCallback(cb);
    });
    m_fillCallback(cb);
}

void LibrarySelectionAspect::requestDisplayText()
{
    ensureFilled();
}

void LibrarySelectionAspect::triggerAction()
{
    ensureFilled();
    QTC_ASSERT(m_model, return);

    auto nameCombo = new QComboBox;
    nameCombo->setInsertPolicy(QComboBox::InsertPolicy::NoInsert);
    nameCombo->setEditable(true);
    nameCombo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    nameCombo->completer()->setFilterMode(Qt::MatchContains);
    nameCombo->setModel(m_model);

    auto versionCombo = new QComboBox;
    versionCombo->addItem("--");

    const auto refreshVersionCombo = [nameCombo, versionCombo] {
        versionCombo->clear();
        versionCombo->addItem("--");
        const QString selected = nameCombo->currentData(SelectedVersion).toString();
        const auto lib = qvariant_cast<Api::Library>(nameCombo->currentData(LibraryData));
        for (const auto &version : std::as_const(lib.versions)) {
            versionCombo->addItem(version.version, version.id);
            if (version.id == selected)
                versionCombo->setCurrentIndex(versionCombo->count() - 1);
        }
    };
    refreshVersionCombo();
    connect(nameCombo, &QComboBox::currentIndexChanged, versionCombo, refreshVersionCombo);

    connect(versionCombo, &QComboBox::activated, this, [this, nameCombo, versionCombo] {
        if (undoStack()) {
            const QVariant old = m_model->data(m_model->index(nameCombo->currentIndex(), 0),
                                               SelectedVersion);
            undoStack()->push(new SelectLibraryVersionCommand(this,
                                                              nameCombo->currentIndex(),
                                                              versionCombo->currentData(),
                                                              old));
        } else {
            m_model->setData(m_model->index(nameCombo->currentIndex(), 0),
                             versionCombo->currentData(),
                             SelectedVersion);
        }
        handleGuiChanged();
        emit displayTextChanged();
    });

    auto clearButton = new QPushButton(Tr::tr("Clear All"));
    connect(clearButton, &QPushButton::clicked, this, [this, refreshVersionCombo] {
        if (undoStack()) {
            undoStack()->beginMacro(Tr::tr("Reset used libraries"));
            for (int i = 0; i < m_model->rowCount(); i++) {
                const QModelIndex idx = m_model->index(i, 0);
                if (idx.data(SelectedVersion).isValid()) {
                    undoStack()->push(new SelectLibraryVersionCommand(this, i, QVariant(),
                                                                      idx.data(SelectedVersion)));
                }
            }
            undoStack()->endMacro();
        } else {
            for (int i = 0; i < m_model->rowCount(); i++)
                m_model->setData(m_model->index(i, 0), QVariant(), SelectedVersion);
        }
        handleGuiChanged();
        refreshVersionCombo();
        emit displayTextChanged();
    });

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);

    QDialog dialog(Utils::dialogParent());
    dialog.setWindowTitle(Tr::tr("Select Libraries"));
    // clang-format off
    Layouting::Column {
        Layouting::Row { nameCombo, versionCombo, clearButton },
        buttons,
    }.attachTo(&dialog);
    // clang-format on
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

#ifdef WITH_TESTS
class LibrarySelectionTest : public QObject
{
    Q_OBJECT

    static QStandardItem *library(const QString &id, const QString &name,
                                  const QList<Api::Library::Version> &versions)
    {
        auto item = new QStandardItem(name);
        Api::Library lib;
        lib.id = id;
        lib.name = name;
        lib.versions = versions;
        item->setData(QVariant::fromValue(lib), LibrarySelectionAspect::LibraryData);
        return item;
    }

private slots:
    void testTheSummaryNamesTheLibrariesAndTheirVersions()
    {
        LibrarySelectionAspect aspect;
        aspect.setFillCallback([](const LibrarySelectionAspect::ResultCallback &cb) {
            cb({library("fmt", "fmt", {{"10.2.1", "fmt1021"}, {"9.1.0", "fmt910"}}),
                library("boost", "Boost", {{"1.84", "boost184"}})});
        });

        // Nothing is asked for until the aspect is drawn: the libraries come
        // from the server. Drawn is what requestDisplayText() means.
        QCOMPARE(aspect.displayText(), QString("No libraries selected"));
        aspect.requestDisplayText();

        // Filled but nothing picked reads as nothing picked, rather than as an
        // empty list of names.
        QCOMPARE(aspect.displayText(), QString("No libraries selected"));

        // A version is stored by its id and shown by its name, because the id
        // is what the server wants and "10.2.1" is what a reader wants.
        aspect.setValue({{"fmt", "fmt1021"}});
        QCOMPARE(aspect.displayText(), QString("fmt 10.2.1"));

        aspect.setValue({{"fmt", "fmt910"}, {"boost", "boost184"}});
        const QString both = aspect.displayText();
        QVERIFY2(both.contains("fmt 9.1.0"), qPrintable(both));
        QVERIFY2(both.contains("Boost 1.84"), qPrintable(both));

        // A version the server no longer offers is shown as the id rather than
        // dropped, so that what is stored is visible.
        aspect.setValue({{"fmt", "fmt700"}});
        QCOMPARE(aspect.displayText(), QString("fmt fmt700"));
    }

    void testItAsksForASummaryAndAButton()
    {
        LibrarySelectionAspect aspect;
        const Utils::AspectPresentation p = aspect.presentation();
        QCOMPARE(p.control, Utils::AspectControls::TextWithAction);
        QVERIFY(!p.actionText.isEmpty());
    }
};

QObject *createLibrarySelectionTest()
{
    return new LibrarySelectionTest;
}
#endif // WITH_TESTS

} // namespace CompilerExplorer

#ifdef WITH_TESTS
#include "compilerexploreraspects.moc"
#endif

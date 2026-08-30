// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codecselector.h"

#include "../coreplugintr.h"
#include "../icore.h"
#include "../textdocument.h"
#include "ioptionspage.h"

#include <utils/aspects.h>
#include <utils/filepath.h>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

#include <QAbstractListModel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <memory>

using namespace Utils;

namespace Core {
namespace Internal {

// Whether \a encoding could have produced \a sample. Decoding and re-encoding
// has to give the bytes back; the shortest-of-the-two comparison is what lets
// a unicode header through, which the encoding adds and the sample has not
// got.
static bool encodingFits(const TextEncoding &encoding, const QByteArray &sample)
{
    if (sample.isEmpty())
        return true;
    const QByteArray verify = encoding.encode(encoding.decode(sample));
    const int minSize = qMin(verify.size(), sample.size());
    if (minSize < sample.size() - 4)
        return false;
    return memcmp(verify.constData() + verify.size() - minSize,
                  sample.constData() + sample.size() - minSize, minSize) == 0;
}

// The encodings offered for \a doc, in the order they are listed. With a
// sample - which there is only when the file would not decode - the list is
// narrowed to the ones that fit it.
static QStringList encodingsFor(BaseTextDocument *doc, const QByteArray &sample)
{
    QStringList names;
    for (const TextEncoding &encoding : TextEncoding::availableEncodings()) {
        if (!doc->supportsEncoding(encoding))
            continue;
        if (!encodingFits(encoding, sample))
            continue;
        names << encoding.fullDisplayName();
    }
    return names;
}

// What the two destructive buttons offer. Reloading throws away what was
// typed, so it is not offered for a modified document; saving cannot be
// trusted for a file that did not decode, because what would be written back
// is not what was read.
struct CodecActions
{
    bool canReload = false;
    bool canSave = false;

    bool operator==(const CodecActions &) const = default;
};

static CodecActions actionsFor(bool modified, bool decodingError, bool hasEncoding)
{
    return {!modified && hasEncoding, !decodingError && hasEncoding};
}

// The encoding a row stands for. A display name may carry aliases after a
// slash - "UTF-8 / unicode-1-1-utf-8" - and the first is its name.
static TextEncoding encodingNamed(const QString &displayName)
{
    if (displayName.isEmpty())
        return {};
    QString name = displayName;
    if (name.contains(" / "))
        name = name.left(name.indexOf(" / "));
    return name.toLatin1();
}

// One column of encoding names. A Quick table asks a model things a list
// widget answered for itself: what its columns are called - nothing, here, so
// that no header appears - and whether a cell may be written to.
class EncodingModel : public QAbstractListModel
{
public:
    void setNames(const QStringList &names)
    {
        beginResetModel();
        m_names = names;
        endResetModel();
    }

    int rowCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : m_names.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_names.size())
            return {};
        switch (role) {
        case Qt::DisplayRole:
            return m_names.at(index.row());
        case AspectTable::EditableRole:
            return false;
        default:
            return {};
        }
    }

    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractListModel::roleNames());
    }

    QString nameAt(int row) const
    {
        return row >= 0 && row < m_names.size() ? m_names.at(row) : QString();
    }

private:
    QStringList m_names;
};

class EncodingTableAspect final : public BaseAspect
{
    Q_OBJECT

public:
    explicit EncodingTableAspect(AspectContainer *container) : BaseAspect(container) {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        // Not something the list widget offered, but there are two hundred
        // encodings and the table asks for one line to narrow them.
        p.filterPlaceholderText = Tr::tr("Filter");
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }
    EncodingModel &model() { return m_model; }

    Q_INVOKABLE void setCurrentRow(int row)
    {
        if (m_currentRow == row)
            return;
        m_currentRow = row;
        emit currentRowChanged();
    }

    // The reader picked a row and meant it, which is the same as choosing it
    // and asking to reload.
    Q_INVOKABLE void activateRow(int row)
    {
        setCurrentRow(row);
        emit rowActivated();
    }

    int currentRow() const { return m_currentRow; }

signals:
    void currentRowChanged();
    void rowActivated();

private:
    EncodingModel m_model;
    int m_currentRow = -1;
};

class CodecSelectorSettings final : public AspectContainer
{
public:
    CodecSelectorSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/CodecSelector.qml"));
        message.setQmlName("Message");
        encodings.setQmlName("Encodings");
    }

    TextDisplay message{this};
    EncodingTableAspect encodings{this};
};

class CodecSelector : public QDialog
{
public:
    explicit CodecSelector(BaseTextDocument *doc);
    ~CodecSelector() override;

    TextEncoding selectedEncoding() const;

private:
    void updateButtons();
    void buttonClicked(QAbstractButton *button);

    bool m_hasDecodingError;
    bool m_isModified;
    const std::unique_ptr<CodecSelectorSettings> m_settings;
    QDialogButtonBox *m_dialogButtonBox;
    QAbstractButton *m_reloadButton;
    QAbstractButton *m_saveButton;

#ifdef WITH_TESTS
    friend class CodecSelectorTest;
#endif
};

CodecSelector::CodecSelector(BaseTextDocument *doc)
    : QDialog(ICore::dialogParent())
    , m_hasDecodingError(doc->hasDecodingError())
    , m_isModified(doc->isModified())
    , m_settings(new CodecSelectorSettings)
{
    setWindowTitle(Tr::tr("Text Encoding"));

    // Only a file that would not decode has a sample to narrow the list with.
    const QByteArray sample = m_hasDecodingError ? doc->decodingErrorSample() : QByteArray();

    QString decodingErrorHint;
    if (m_hasDecodingError)
        decodingErrorHint = '\n' + Tr::tr("The following encodings are likely to fit:");
    m_settings->message.setText(Tr::tr("Select encoding for \"%1\".%2")
                                    .arg(doc->filePath().fileName())
                                    .arg(decodingErrorHint));

    const QStringList names = encodingsFor(doc, sample);
    m_settings->encodings.model().setNames(names);
    m_settings->encodings.setCurrentRow(
        names.indexOf(doc->encoding().fullDisplayName()));

    m_dialogButtonBox = new QDialogButtonBox(this);
    m_reloadButton = m_dialogButtonBox->addButton(Tr::tr("Reload with Encoding"),
                                                  QDialogButtonBox::DestructiveRole);
    m_saveButton = m_dialogButtonBox->addButton(Tr::tr("Save with Encoding"),
                                                QDialogButtonBox::DestructiveRole);
    m_dialogButtonBox->addButton(QDialogButtonBox::Cancel);
    connect(m_dialogButtonBox, &QDialogButtonBox::clicked, this, &CodecSelector::buttonClicked);

    connect(&m_settings->encodings, &EncodingTableAspect::currentRowChanged,
            this, &CodecSelector::updateButtons);
    connect(&m_settings->encodings, &EncodingTableAspect::rowActivated,
            this, [this] {
                if (m_reloadButton->isEnabled())
                    m_reloadButton->click();
            });

    auto vbox = new QVBoxLayout(this);
    vbox->addWidget(Core::createAspectForm(m_settings.get()));
    vbox->addWidget(m_dialogButtonBox);

    updateButtons();
}

CodecSelector::~CodecSelector() = default;

void CodecSelector::updateButtons()
{
    const CodecActions actions
        = actionsFor(m_isModified, m_hasDecodingError, selectedEncoding().isValid());
    m_reloadButton->setEnabled(actions.canReload);
    m_saveButton->setEnabled(actions.canSave);
}

TextEncoding CodecSelector::selectedEncoding() const
{
    return encodingNamed(m_settings->encodings.model().nameAt(
        m_settings->encodings.currentRow()));
}

void CodecSelector::buttonClicked(QAbstractButton *button)
{
    CodecSelectorResult::Action result = CodecSelectorResult::Cancel;
    if (button == m_reloadButton)
        result = CodecSelectorResult::Reload;
    if (button == m_saveButton)
        result = CodecSelectorResult::Save;
    done(result);
}

#ifdef WITH_TESTS

class CodecSelectorTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        CodecSelectorSettings settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "CodecSelector.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhichEncodingARowStandsFor()
    {
        // A display name may carry aliases after a slash, and the first of
        // them is the name to ask for.
        QCOMPARE(QString::fromLatin1(encodingNamed("UTF-8").name()), QString("UTF-8"));
        QCOMPARE(QString::fromLatin1(encodingNamed("UTF-8 / unicode-1-1-utf-8").name()),
                 QString("UTF-8"));
        QVERIFY2(!encodingNamed(QString()).isValid(), "no row named an encoding");
    }

    void testWhichEncodingsCouldHaveWrittenTheFile()
    {
        // The sample is what would not decode, so an encoding is offered only
        // if putting it back gives the same bytes.
        const TextEncoding utf8("UTF-8");
        QVERIFY(utf8.isValid());

        // Plain ASCII round-trips through anything ASCII-compatible.
        QVERIFY2(encodingFits(utf8, "hello"), "UTF-8 could not have written ASCII");

        // A byte that is not valid UTF-8 cannot have been written by it.
        const QByteArray latin1Only("caf\xe9");
        QVERIFY2(!encodingFits(utf8, latin1Only),
                 "UTF-8 was offered for bytes it cannot produce");

        const TextEncoding latin1("ISO-8859-1");
        if (latin1.isValid())
            QVERIFY2(encodingFits(latin1, latin1Only), "Latin-1 was not offered for its own bytes");

        // With nothing to check against, everything fits - which is the case
        // for a file that decoded properly.
        QVERIFY(encodingFits(utf8, {}));
    }

    void testWhenReloadingAndSavingAreOffered()
    {
        // Nothing wrong and something chosen: both.
        QCOMPARE(actionsFor(false, false, true), (CodecActions{true, true}));

        // Reloading throws away what was typed.
        QCOMPARE(actionsFor(true, false, true), (CodecActions{false, true}));

        // Saving a file that did not decode would write back something other
        // than what was read.
        QCOMPARE(actionsFor(false, true, true), (CodecActions{true, false}));

        // And with no encoding chosen there is nothing to do either way.
        QCOMPARE(actionsFor(false, false, false), (CodecActions{false, false}));
    }

    void testTheListSaysNothingAboutItsColumn()
    {
        // One unnamed column: a Quick table draws a header saying "1" for a
        // model that answers the column number, which a list of encodings has
        // no use for.
        CodecSelectorSettings settings;
        settings.encodings.model().setNames({"UTF-8", "ISO-8859-1"});

        QAbstractItemModel *const model = settings.encodings.tableModel();
        QCOMPARE(model->rowCount(), 2);
        QVERIFY2(!model->headerData(0, Qt::Horizontal).isValid(),
                 "the encoding list named its column");
        QVERIFY2(!model->data(model->index(0, 0), AspectTable::EditableRole).toBool(),
                 "an encoding could be edited");
        QCOMPARE(model->data(model->index(1, 0), Qt::DisplayRole).toString(),
                 QString("ISO-8859-1"));
    }

    void testPickingARowIsWhatTheDialogAnswers()
    {
        CodecSelectorSettings settings;
        settings.encodings.model().setNames({"UTF-8", "ISO-8859-1"});

        QVERIFY2(settings.encodings.currentRow() < 0, "a row was chosen before anything was");

        QSignalSpy rows(&settings.encodings, &EncodingTableAspect::currentRowChanged);
        settings.encodings.setCurrentRow(1);
        QCOMPARE(rows.count(), 1);
        QCOMPARE(settings.encodings.model().nameAt(1), QString("ISO-8859-1"));

        // Saying the same thing again is not a change.
        settings.encodings.setCurrentRow(1);
        QCOMPARE(rows.count(), 1);
    }

    void testActivatingARowInTheDrawnTableAsksToReload()
    {
        // The .qml has to hand both the row and the activation back; a C++
        // test calling them itself would not notice if either line went.
        CodecSelectorSettings settings;
        settings.encodings.model().setNames({"UTF-8", "ISO-8859-1"});

        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject *const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the dialog was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("encodingTable"));

        QSignalSpy activated(&settings.encodings, &EncodingTableAspect::rowActivated);
        QVERIFY(QMetaObject::invokeMethod(table, "rowActivated", Q_ARG(int, 1)));
        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(settings.encodings.currentRow(), 1);
    }
};

QObject *createCodecSelectorTest()
{
    return new CodecSelectorTest;
}

#endif // WITH_TESTS

} // namespace Internal

CodecSelectorResult askForCodec(BaseTextDocument *doc)
{
    Internal::CodecSelector dialog(doc);
    const CodecSelectorResult::Action result = CodecSelectorResult::Action(dialog.exec());
    return {result, dialog.selectedEncoding()};
}

} // namespace Core

#include "codecselector.moc"

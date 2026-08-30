// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pasteview.h"

#include "columnindicatortextedit.h"
#include "cpastertr.h"
#include "protocol.h"
#include "settings.h"
#include "splitter.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/layoutbuilder.h>
#include <utils/mimeconstants.h>
#ifdef WITH_TESTS
#include <QTest>
#endif

#include <utils/aspects.h>
#include <utils/qtcassert.h>
#include <utils/qtcsettings.h>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextEdit>

using namespace Utils;

namespace CodePaster {

const char groupC[] = "CPaster";
const char heightKeyC[] = "PasteViewHeight";
const char widthKeyC[] = "PasteViewWidth";

static ContentType contentType(const QString &mt)
{
    using namespace Utils::Constants;
    if (mt == QLatin1StringView(C_SOURCE_MIMETYPE)
        || mt == QLatin1StringView(C_HEADER_MIMETYPE)
        || mt == QLatin1StringView(GLSL_MIMETYPE)
        || mt == QLatin1StringView(GLSL_VERT_MIMETYPE)
        || mt == QLatin1StringView(GLSL_FRAG_MIMETYPE)
        || mt == QLatin1StringView(GLSL_ES_VERT_MIMETYPE)
        || mt == QLatin1StringView(GLSL_ES_FRAG_MIMETYPE))
        return C;
    if (mt == QLatin1StringView(CPP_SOURCE_MIMETYPE)
        || mt == QLatin1StringView(CPP_HEADER_MIMETYPE)
        || mt == QLatin1StringView(OBJECTIVE_C_SOURCE_MIMETYPE)
        || mt == QLatin1StringView(OBJECTIVE_CPP_SOURCE_MIMETYPE))
        return Cpp;
    if (mt == QLatin1StringView(QML_MIMETYPE)
        || mt == QLatin1StringView(QMLUI_MIMETYPE)
        || mt == QLatin1StringView(QMLPROJECT_MIMETYPE)
        || mt == QLatin1StringView(QBS_MIMETYPE)
        || mt == QLatin1StringView(JS_MIMETYPE)
        || mt == QLatin1StringView(JSON_MIMETYPE))
        return JavaScript;
    if (mt == QLatin1StringView("text/x-patch"))
        return Diff;
    if (mt == QLatin1StringView("text/xml")
        || mt == QLatin1StringView("application/xml")
        || mt == QLatin1StringView(RESOURCE_MIMETYPE)
        || mt == QLatin1StringView(FORM_MIMETYPE))
        return Xml;
    return Text;
}

// The parts of a diff and whether each is being sent. The list is checkable,
// which a Qt Quick table asks the model about rather than reading flags.
class PartsModel : public QAbstractListModel
{
public:
    void setParts(const FileDataList &parts)
    {
        beginResetModel();
        m_parts = parts;
        m_checked = QList<bool>(parts.size(), true);
        endResetModel();
    }

    // What would be sent: the content of every part still checked.
    QString content() const
    {
        QString result;
        for (int i = 0; i < m_parts.size(); ++i) {
            if (m_checked.value(i))
                result += m_parts.at(i).content;
        }
        return result;
    }

    int rowCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : m_parts.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_parts.size())
            return {};
        switch (role) {
        case Qt::DisplayRole:
            return m_parts.at(index.row()).filename;
        case Qt::CheckStateRole:
            return m_checked.value(index.row()) ? Qt::Checked : Qt::Unchecked;
        case AspectTable::CheckableRole:
            return true;
        case AspectTable::EditableRole:
            return false;
        default:
            return {};
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (role != Qt::CheckStateRole || !index.isValid() || index.row() >= m_checked.size())
            return false;
        m_checked[index.row()] = value.toInt() != Qt::Unchecked;
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return QAbstractListModel::flags(index) | Qt::ItemIsUserCheckable;
    }

    // One unnamed column: a table draws a header saying "1" otherwise.
    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractListModel::roleNames());
    }

private:
    FileDataList m_parts;
    QList<bool> m_checked;
};

// What a paste is sent as. An empty user name is posted anonymously, which is
// what the server sees when nobody says who they are.
QString pasteUser(const QString &typed);

class PasteViewSettings;

class PasteView : public QDialog
{
public:
    enum Mode
    {
        // Present a list of read-only diff chunks which the user can check for inclusion
        DiffChunkMode,
        // Present plain, editable text.
        PlainTextMode
    };

    explicit PasteView(const QList<Protocol *> &protocols);
    ~PasteView() override;

    // Show up with checkable list of diff chunks.
    int show(const QString &user, int expiryDays, const FileDataList &parts);
    // Show up with editable plain text.
    int show(const QString &user, int expiryDays, const QString &content);

    void setProtocol(const QString &protocol);

    QString user() const;
    QString description() const;
    QString content() const;
    int protocol() const;
    void setExpiryDays(int d);
    int expiryDays() const;

    void accept() override;

private:
    void contentChanged();
    void protocolChanged(int);

    int showDialog();

    const QList<Protocol *> m_protocols;

    const std::unique_ptr<PasteViewSettings> m_settings;
    FileDataList m_parts;
    Mode m_mode = DiffChunkMode;

#ifdef WITH_TESTS
    friend class PasteViewTest;
#endif
};

QString pasteUser(const QString &typed)
{
    return typed.isEmpty() ? QLatin1String("Anonymous") : typed;
}

class PasteViewSettings final : public AspectContainer
{
public:
    PasteViewSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CodePaster/PasteView.qml"));

        protocol.setQmlName("Protocol");
        protocol.setLabelText(Tr::tr("Protocol:"));
        protocol.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

        expiry.setQmlName("Expiry");
        expiry.setLabelText(Tr::tr("&Expires after:"));
        expiry.setRange(1, 365);
        expiry.setSuffix(Tr::tr(" Days"));

        username.setQmlName("Username");
        username.setLabelText(Tr::tr("&Username:"));
        username.setDisplayStyle(StringAspect::LineEditDisplay);
        username.setPlaceHolderText(Tr::tr("<Username>"));

        description.setQmlName("Description");
        description.setLabelText(Tr::tr("&Description:"));
        description.setDisplayStyle(StringAspect::LineEditDisplay);
        description.setPlaceHolderText(Tr::tr("<Description>"));

        parts.setQmlName("Parts");
        parts.setLabelText(Tr::tr("Parts to Send to Server"));
        parts.setModel(&partsModel);

        preview.setQmlName("Preview");
        preview.setDisplayStyle(StringAspect::TextEditDisplay);
        preview.setReadOnly(true);
        // A diff's columns line up.
        preview.setMonospace(true);

        plainText.setQmlName("PlainText");
        plainText.setDisplayStyle(StringAspect::TextEditDisplay);
        plainText.setMonospace(true);
    }

    // The two ways of choosing what to send: a list of diff chunks with a
    // preview of the lot, or one editable block of text. The widget dialog
    // stacked them; here each says whether it is the one in use.
    void setMode(bool plainTextMode)
    {
        parts.setVisible(!plainTextMode);
        preview.setVisible(!plainTextMode);
        plainText.setVisible(plainTextMode);
    }

    // What the chosen protocol will accept.
    void setCapabilities(Capabilities caps)
    {
        description.setEnabled(caps & Capability::PostDescription);
        username.setEnabled(caps & Capability::PostUserName);
    }

    PartsModel partsModel;
    SelectionAspect protocol{this};
    IntegerAspect expiry{this};
    StringAspect username{this};
    StringAspect description{this};
    TableAspect parts{this};
    StringAspect preview{this};
    StringAspect plainText{this};
};

PasteView::PasteView(const QList<Protocol *> &protocols)
    : QDialog(Core::ICore::dialogParent())
    , m_protocols(protocols)
    , m_settings(new PasteViewSettings)
{
    setObjectName("CodePaster.ViewDialog");
    resize(670, 678);
    setWindowTitle(Tr::tr("Send to Codepaster"));

    for (const Protocol *p : protocols)
        m_settings->protocol.addOption(p->name());

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttonBox->button(QDialogButtonBox::Ok)->setText(Tr::tr("Paste"));

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    // What is sent follows what is still checked.
    connect(&m_settings->partsModel, &QAbstractItemModel::dataChanged,
            this, &PasteView::contentChanged);
    connect(&m_settings->protocol, &BaseAspect::changed, this, [this] {
        protocolChanged(m_settings->protocol.value());
    });

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

PasteView::~PasteView() = default;

QString PasteView::user() const
{
    return pasteUser(m_settings->username());
}

QString PasteView::description() const
{
    return m_settings->description();
}

QString PasteView::content() const
{
    if (m_mode == PlainTextMode)
        return m_settings->plainText();
    return m_settings->partsModel.content();
}

int PasteView::protocol() const
{
    return m_settings->protocol.value();
}

void PasteView::contentChanged()
{
    m_settings->preview.setValue(content());
}

void PasteView::protocolChanged(int p)
{
    QTC_ASSERT(p >= 0 && p < m_protocols.size(), return);
    m_settings->setCapabilities(m_protocols.at(p)->capabilities());
}

int PasteView::showDialog()
{
    m_settings->description.setFocusToInputField();

    // (Re)store dialog size
    const QtcSettings *settings = Core::ICore::settings();
    const Key rootKey = Key(groupC) + '/';
    const int h = settings->value(rootKey + heightKeyC, height()).toInt();
    const int w = settings->value(rootKey + widthKeyC, width()).toInt();

    resize(w, h);

    return QDialog::exec();
}

// Show up with checkable list of diff chunks.
int PasteView::show(const QString &user, int expiryDays, const FileDataList &parts)
{
    m_settings->username.setValue(user);
    m_parts = parts;
    m_mode = DiffChunkMode;
    m_settings->partsModel.setParts(parts);
    m_settings->setMode(false);
    contentChanged();
    setExpiryDays(expiryDays);
    return showDialog();
}

// Show up with editable plain text.
int PasteView::show(const QString &user, int expiryDays, const QString &content)
{
    m_settings->username.setValue(user);
    m_mode = PlainTextMode;
    m_settings->setMode(true);
    m_settings->plainText.setValue(content);
    setExpiryDays(expiryDays);
    return showDialog();
}

void PasteView::setExpiryDays(int d)
{
    m_settings->expiry.setValue(d);
}

int PasteView::expiryDays() const
{
    return m_settings->expiry();
}

void PasteView::accept()
{
    const int index = m_settings->protocol.value();
    if (index == -1)
        return;

    Protocol *protocol = m_protocols.at(index);

    if (!Protocol::ensureConfiguration(protocol))
        return;

    const QString data = content();
    if (data.isEmpty())
        return;

    // Store settings and close
    QtcSettings *settings = Core::ICore::settings();
    settings->beginGroup(groupC);
    settings->setValue(heightKeyC, height());
    settings->setValue(widthKeyC, width());
    settings->endGroup();
    QDialog::accept();
}

void PasteView::setProtocol(const QString &protocol)
{
    const int index = m_settings->protocol.indexForDisplay(protocol);
    if (index < 0)
        return;
    m_settings->protocol.setValue(index);
    // Setting the same value again says nothing, so the fields are enabled
    // from here rather than from the change.
    protocolChanged(index);
}

static inline void fixSpecialCharacters(QString &data)
{
    QChar *uc = data.data();
    QChar *e = uc + data.size();

    for (; uc != e; ++uc) {
        switch (uc->unicode()) {
        case 0xfdd0: // QTextBeginningOfFrame
        case 0xfdd1: // QTextEndOfFrame
        case QChar::ParagraphSeparator:
        case QChar::LineSeparator:
            *uc = QLatin1Char('\n');
            break;
        case QChar::Nbsp:
            *uc = QLatin1Char(' ');
            break;
        default:
            break;
        }
    }
}

std::optional<PasteInputData> executePasteDialog(const QList<Protocol *> &protocols,
                                                 const QString &data, const QString &mimeType)
{
    QString copiedData = data;
    fixSpecialCharacters(copiedData);

    const QString username = settings().username();

    PasteView view(protocols);
    view.setProtocol(settings().protocols.stringValue());

    const FileDataList diffChunks = splitDiffToFiles(copiedData);
    const int dialogResult = diffChunks.isEmpty()
                                 ? view.show(username, settings().expiryDays(), copiedData)
                                 : view.show(username, settings().expiryDays(), diffChunks);
    if (dialogResult != QDialog::Accepted)
        return {};

    // Save new protocol in case user changed it.
    if (settings().protocols() != view.protocol()) {
        settings().protocols.setValue(view.protocol());
        settings().writeSettings();
    }

    return PasteInputData{view.content(), contentType(mimeType), view.expiryDays(),
                          view.user(), view.description()};
}

#ifdef WITH_TESTS

class PasteViewTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        PasteViewSettings settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "PasteView.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhoAPasteIsFrom()
    {
        // Nobody typed a name, so the server is told nobody.
        QCOMPARE(pasteUser({}), QString("Anonymous"));
        QCOMPARE(pasteUser("someone"), QString("someone"));
    }

    void testWhatIsSentIsWhatIsStillChecked()
    {
        PartsModel model;
        FileDataList parts;
        parts.append(FileData("a.cpp", "AAA"));
        parts.append(FileData("b.cpp", "BBB"));
        parts.append(FileData("c.cpp", "CCC"));
        model.setParts(parts);

        // Everything is sent unless it is taken out.
        QCOMPARE(model.rowCount({}), 3);
        QCOMPARE(model.content(), QString("AAABBBCCC"));

        // Unchecking a part leaves it out, and the rest keep their order.
        QVERIFY(model.setData(model.index(1, 0), Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(model.content(), QString("AAACCC"));
        QCOMPARE(model.data(model.index(1, 0), Qt::CheckStateRole).toInt(), int(Qt::Unchecked));

        // And putting it back puts its content back where it was.
        QVERIFY(model.setData(model.index(1, 0), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(model.content(), QString("AAABBBCCC"));
    }

    void testThePartsAreCheckableAndNotEditable()
    {
        // A Quick table asks the model both of these; the file names are
        // chosen from, not typed over.
        PartsModel model;
        FileDataList parts;
        parts.append(FileData("a.cpp", "AAA"));
        model.setParts(parts);

        const QModelIndex index = model.index(0, 0);
        QVERIFY2(model.data(index, AspectTable::CheckableRole).toBool(),
                 "a part could not be taken out of the paste");
        const QVariant editable = model.data(index, AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(!editable.toBool(), "a file name could be typed over");
        QVERIFY2(!model.headerData(0, Qt::Horizontal, Qt::DisplayRole).isValid(),
                 "the list of parts named its column");
    }

    void testOnlyOneWayOfChoosingIsShown()
    {
        // A diff is chosen from a list with a preview; plain text is one block
        // to edit. The widget dialog stacked them, so exactly one shows.
        PasteViewSettings settings;

        settings.setMode(false);
        QVERIFY(settings.parts.isVisible());
        QVERIFY(settings.preview.isVisible());
        QVERIFY2(!settings.plainText.isVisible(), "both ways of choosing were shown");

        settings.setMode(true);
        QVERIFY(settings.plainText.isVisible());
        QVERIFY2(!settings.parts.isVisible(), "the chunk list showed for a plain paste");
        QVERIFY2(!settings.preview.isVisible(), "the preview showed for a plain paste");
    }

    void testWhatTheProtocolWillAccept()
    {
        // A server that takes neither a description nor a name is not asked
        // for them.
        PasteViewSettings settings;
        settings.setCapabilities({});
        QVERIFY2(!settings.description.isEnabled(), "a description was asked for regardless");
        QVERIFY2(!settings.username.isEnabled(), "a user name was asked for regardless");

        settings.setCapabilities(Capability::PostDescription);
        QVERIFY(settings.description.isEnabled());
        QVERIFY2(!settings.username.isEnabled(), "a user name was asked for regardless");

        settings.setCapabilities(Capability::PostDescription | Capability::PostUserName);
        QVERIFY(settings.description.isEnabled());
        QVERIFY(settings.username.isEnabled());
    }

    void testHowLongAPasteLives()
    {
        PasteViewSettings settings;
        QCOMPARE(settings.expiry.presentation().minimum.toInt(), 1);
        QCOMPARE(settings.expiry.presentation().maximum.toInt(), 365);
        QVERIFY2(!settings.expiry.presentation().suffix.isEmpty(),
                 "the number of days did not say it was days");
    }
};

QObject *createPasteViewTest()
{
    return new PasteViewTest;
}

#endif // WITH_TESTS

} // CodePaster

#ifdef WITH_TESTS
#include "pasteview.moc"
#endif

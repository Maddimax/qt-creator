// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "topicchooser.h"

#include "helptr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/helplink.h>
#include <coreplugin/icore.h>

#include <utils/aspects.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QUrl>
#include <QVBoxLayout>

using namespace Utils;

const int kInitialWidth = 400;
const int kInitialHeight = 220;
const char kPreferenceDialogSize[] = "Core/TopicChooserSize";

namespace Help::Internal {

// What the dialog asks. The keyword is set apart, so this is markup.
QString chooseTopicQuestion(const QString &keyword)
{
    return ::Help::Tr::tr("Choose a topic for %1:").arg("<b>" + keyword + "</b>");
}

// The topics offered for one keyword: what each is called, with the address it
// leads to on hover.
class TopicsModel final : public QAbstractTableModel
{
public:
    void setLinks(const QList<Core::HelpLink> &links)
    {
        beginResetModel();
        m_links = links;
        endResetModel();
    }

    QUrl urlAt(int row) const
    {
        return row >= 0 && row < m_links.size() ? m_links.at(row).url : QUrl();
    }

    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : m_links.size(); }
    int columnCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : 1; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == AspectTable::EditableRole)
            return false;
        if (!index.isValid() || index.row() >= m_links.size())
            return {};
        switch (role) {
        case Qt::DisplayRole: return m_links.at(index.row()).title;
        case Qt::ToolTipRole: return m_links.at(index.row()).url.toString();
        default: return {};
        }
    }

    // One nameless column, so no heading.
    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    { return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags; }

    QHash<int, QByteArray> roleNames() const override
    { return AspectTable::withRoleNames(QAbstractTableModel::roleNames()); }

private:
    QList<Core::HelpLink> m_links;
};

class TopicChooserSettings final : public AspectContainer
{
public:
    TopicChooserSettings(const QString &keyword, const QList<Core::HelpLink> &links)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Help/TopicChooser.qml"));

        question.setQmlName("Question");
        question.setTextFormat(AspectControls::TextFormat::RichText);
        question.setText(chooseTopicQuestion(keyword));

        topics.setQmlName("Topics");
        topics.setModel(&model);
        // The reader arrives here having typed a word; the field above the
        // list is how they narrow a long answer, and the arrows below it walk
        // the rows without leaving it.
        topics.setFilterPlaceholderText(::Help::Tr::tr("Filter"));

        model.setLinks(links);
        // Opened on the first topic, so that Return takes it without anything
        // else being done.
        topics.setCurrentRow(links.isEmpty() ? -1 : 0);
    }

    QUrl chosenLink() const { return model.urlAt(topics.currentRow()); }

    TextDisplay question{this};
    TableAspect topics{this};
    TopicsModel model;
};

} // namespace Help::Internal

TopicChooser::TopicChooser(QWidget *parent, const QString &keyword,
        const QList<Core::HelpLink> &links)
    : QDialog(parent)
    , d(new Help::Internal::TopicChooserSettings(keyword, links))
{
    const QSize initialSize(kInitialWidth, kInitialHeight);
    resize(Core::ICore::settings()->value(kPreferenceDialogSize, initialSize).toSize());

    setWindowTitle(::Help::Tr::tr("Choose Topic"));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(d.get()));
    layout->addWidget(buttonBox);

    // A topic chosen and meant - Return, or a double click - is the same as
    // pressing Ok.
    connect(&d->topics, &TableAspect::rowActivated, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

TopicChooser::~TopicChooser()
{
    Core::ICore::settings()->setValueWithDefault(kPreferenceDialogSize,
                                           size(),
                                           QSize(kInitialWidth, kInitialHeight));
}

QUrl TopicChooser::link() const
{
    return d->chosenLink();
}

namespace Help::Internal {

#ifdef WITH_TESTS

class TopicChooserTest final : public QObject
{
    Q_OBJECT

private:
    static QList<Core::HelpLink> twoTopics()
    {
        return {{QUrl("qthelp://org.qt-project.qtcreator/doc/one.html"), "One"},
                {QUrl("qthelp://org.qt-project.qtcreator/doc/two.html"), "Two"}};
    }

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        TopicChooserSettings settings("keyword", twoTopics());
        const Result<> rendered = Core::aspectFormRenders(&settings, "TopicChooser.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheQuestionNamesTheKeyword()
    {
        const QString question = chooseTopicQuestion("QObject");
        QVERIFY2(question.contains("QObject"), "the question does not say which word");
        QVERIFY2(question.contains("<b>"), "the word is not set apart from the question");

        TopicChooserSettings settings("QObject", {});
        QCOMPARE(settings.question.presentation().textFormat,
                 AspectControls::TextFormat::RichText);
    }

    void testTheDialogOpensOnTheFirstTopic()
    {
        // So that Return takes one without anything else being done, which is
        // what the widget list did by setting the current index itself.
        TopicChooserSettings settings("keyword", twoTopics());
        QCOMPARE(settings.topics.currentRow(), 0);
        QCOMPARE(settings.chosenLink(), twoTopics().at(0).url);

        settings.topics.setCurrentRow(1);
        QCOMPARE(settings.chosenLink(), twoTopics().at(1).url);
    }

    void testNoTopicsIsNoLink()
    {
        TopicChooserSettings settings("keyword", {});
        QCOMPARE(settings.topics.currentRow(), -1);
        QVERIFY(settings.chosenLink().isEmpty());
    }

    void testARowIsTheTopicItStandsFor()
    {
        // The widget mapped the chosen row back through the filter proxy to
        // reach the link; the table reports in the model's own rows, so the
        // row is the index.
        TopicsModel model;
        model.setLinks(twoTopics());
        QCOMPARE(model.urlAt(1), twoTopics().at(1).url);
        QVERIFY(model.urlAt(-1).isEmpty());
        QVERIFY(model.urlAt(2).isEmpty());
    }

    void testATopicSaysWhereItLeads()
    {
        // The widget item carried the address as a tooltip; a Quick cell reads
        // it by name, so the role has to be named as well as answered.
        TopicsModel model;
        model.setLinks(twoTopics());
        QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QString("One"));
        QVERIFY2(model.data(model.index(0, 0), Qt::ToolTipRole).toString().contains("one.html"),
                 "a topic does not say where it leads");
        QVERIFY(model.roleNames().values().contains("cellToolTip"));
    }

    void testTheListIsNarrowedNotTypedIn()
    {
        TopicChooserSettings settings("keyword", twoTopics());
        QVERIFY2(!settings.topics.presentation().filterPlaceholderText.isEmpty(),
                 "a long list of topics cannot be narrowed");

        const QVariant editable
            = settings.model.data(settings.model.index(0, 0), AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(!editable.toBool(), "a topic could be renamed by typing in the list");
    }
};

QObject *createTopicChooserTest()
{
    return new TopicChooserTest;
}

#endif // WITH_TESTS

} // namespace Help::Internal

#ifdef WITH_TESTS
#include "topicchooser.moc"
#endif

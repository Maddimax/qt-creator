// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "scxmldocument.h"
#include "scxmleditortr.h"
#include "scxmltag.h"
#include "statistics.h"

#include <utils/layoutbuilder.h>

#include <coreplugin/dialogs/ioptionspage.h>

#include <QDateTime>

#ifdef WITH_TESTS
#include <QTest>
#endif
#include <QSortFilterProxyModel>

using namespace ScxmlEditor::PluginInterface;
using namespace ScxmlEditor::Common;

StatisticsModel::StatisticsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void StatisticsModel::calculateStats(ScxmlTag *tag)
{
    // Calculate depth
    int level = -1;
    ScxmlTag *levelTag = tag;
    if (levelTag->tagType() != State && levelTag->tagType() != Parallel)
        levelTag = levelTag->parentTag();
    while (levelTag) {
        level++;
        levelTag = levelTag->parentTag();
    }
    if (level > m_levels)
        m_levels = level;

    // Calculate statistics
    QString tagName = tag->tagName();
    if (m_names.contains(tagName))
        m_counts[m_names.indexOf(tagName)]++;
    else {
        m_names << tagName;
        m_counts << 1;
    }

    for (int i = 0; i < tag->childCount(); ++i)
        calculateStats(tag->child(i));
}

void StatisticsModel::setDocument(ScxmlDocument *document)
{
    beginResetModel();
    m_names.clear();
    m_counts.clear();
    m_levels = 0;

    if (document)
        calculateStats(document->scxmlRootTag());

    endResetModel();
}

int StatisticsModel::levels() const
{
    return m_levels;
}
int StatisticsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_names.count();
}

int StatisticsModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 2;
}

QVariant StatisticsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0:
            return Tr::tr("Tag");
        case 1:
            return Tr::tr("Count");
        default:
            break;
        }
    }

    return QVariant();
}

QVariant StatisticsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || role != Qt::DisplayRole)
        return QVariant();

    int row = index.row();
    if (row >= 0 && row < m_names.count()) {
        switch (index.column()) {
        case 0:
            return m_names[row];
        case 1:
            return m_counts[row];
        default:
            break;
        }
    }

    return QVariant();
}

// The counts, as an aspect the form draws as a tree. The model is the one that
// was here before; what changed is that a form asks the aspect for it rather
// than a view being handed it directly.
class Statistics::TagCounts final : public Utils::BaseAspect
{
public:
    explicit TagCounts(Utils::AspectContainer *container)
        : Utils::BaseAspect(container)
        // Parented, so that QML cannot take ownership of a model and delete it.
        , m_model(new StatisticsModel(this))
        , m_sorted(new QSortFilterProxyModel(this))
    {
        setQmlName("Counts");
        m_sorted->setFilterKeyColumn(-1);
        m_sorted->setSourceModel(m_model);
    }

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = Utils::BaseAspect::presentation();
        p.control = Utils::AspectControls::Tree;
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_sorted; }

    void setDocument(ScxmlDocument *document) { m_model->setDocument(document); }
    int levels() const { return m_model->levels(); }

private:
    StatisticsModel *m_model = nullptr;
    QSortFilterProxyModel *m_sorted = nullptr;
};

Statistics::Statistics()
    : m_counts(new TagCounts(this))
{
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ScxmlEditor/Statistics.qml"));

    fileName.setQmlName("FileName");
    fileName.setLabelText(Tr::tr("File"));

    time.setQmlName("Time");
    time.setLabelText(Tr::tr("Time"));
    time.setText(QDateTime::currentDateTime().toString(Tr::tr("yyyy/MM/dd hh:mm:ss")));

    levels.setQmlName("Levels");
    levels.setLabelText(Tr::tr("Max. levels"));
}

Statistics::~Statistics() = default;

void Statistics::setDocument(ScxmlDocument *doc)
{
    fileName.setText(doc->filePath().toUserOutput());
    m_counts->setDocument(doc);
    levels.setText(QString::number(m_counts->levels()));
}

#ifdef WITH_TESTS

namespace ScxmlEditor::Common {

class StatisticsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheCountsAreShownAsAForm()
    {
        Statistics statistics;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&statistics, "Statistics.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        // The time is filled in when the form is built rather than when a
        // document arrives: the dialog reports when it was opened.
        QVERIFY2(!statistics.time.text().isEmpty(), "the form does not say when it was taken");

        // Four things are shown - the three the file says and the tree of
        // counts - and the tree is one the aspect hands over rather than a view
        // that was handed a model.
        QCOMPARE(statistics.aspects().size(), 4);
    }
};

QObject *createStatisticsTest()
{
    return new StatisticsTest;
}

} // namespace ScxmlEditor::Common

#include "statistics.moc"

#endif // WITH_TESTS

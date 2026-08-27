// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QAbstractTableModel>
#include <utils/aspects.h>

QT_BEGIN_NAMESPACE
class QLabel;
class QSortFilterProxyModel;
QT_END_NAMESPACE

namespace Utils { class TreeView; }

namespace ScxmlEditor {

namespace PluginInterface {
class ScxmlDocument;
class ScxmlTag;
} // namespace PluginInterface

namespace Common {

class StatisticsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    StatisticsModel(QObject *parent = nullptr);
    void setDocument(PluginInterface::ScxmlDocument *document);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    int levels() const;

private:
    void calculateStats(PluginInterface::ScxmlTag *tag);

    QStringList m_names;
    QList<int> m_counts;
    int m_levels = 0;
};

// The counts a document adds up to, as a form: three things it says about the
// file and a tree of what is in it. An aspect container rather than a widget,
// so that the dialog around it only has to ask for a form.
class Statistics : public Utils::AspectContainer
{
    Q_OBJECT

public:
    Statistics();
    ~Statistics() override;

    void setDocument(PluginInterface::ScxmlDocument *doc);

    Utils::TextDisplay fileName{this};
    Utils::TextDisplay time{this};
    Utils::TextDisplay levels{this};

private:
    class TagCounts;
    TagCounts *m_counts = nullptr;
};

#ifdef WITH_TESTS
QObject *createStatisticsTest();
#endif

} // namespace Common
} // namespace ScxmlEditor

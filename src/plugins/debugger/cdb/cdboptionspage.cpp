// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cdboptionspage.h"

#include <debugger/debuggeractions.h>
#include <debugger/debuggercore.h>
#include <debugger/debuggerinternalconstants.h>
#include <debugger/debuggertr.h>

#include <utils/aspectpresentation.h>
#include <utils/aspects.h>
#include <utils/guiutils.h>

#include <QAbstractTableModel>

using namespace Utils;

namespace Debugger::Internal {

struct EventsDescription {
    const char *abbreviation;
    bool hasParameter;
    const char *description;
};

// Parameters of the "sxe" command
const EventsDescription eventDescriptions[] =
{
    {"eh", false, QT_TRANSLATE_NOOP("QtC::Debugger", "C++ exception")},
    {"ct", false, QT_TRANSLATE_NOOP("QtC::Debugger", "Thread creation")},
    {"et", false, QT_TRANSLATE_NOOP("QtC::Debugger", "Thread exit")},
    {"ld", true,  QT_TRANSLATE_NOOP("QtC::Debugger", "Load module:")},
    {"ud", true,  QT_TRANSLATE_NOOP("QtC::Debugger", "Unload module:")},
    {"out", true, QT_TRANSLATE_NOOP("QtC::Debugger", "Output:")}
};

static inline int indexOfEvent(const QString &abbrev)
{
    const size_t eventCount = sizeof(eventDescriptions) / sizeof(EventsDescription);
    for (size_t e = 0; e < eventCount; e++)
        if (abbrev == QLatin1String(eventDescriptions[e].abbreviation))
                return int(e);
    return -1;
}

// ---------- CdbOptionsPage

// Which "sxe" events stop the debugger, and the filter text for the three that
// take one. The rows are the known events - they cannot be added to or removed
// - so what the user changes is a check state and a parameter.
class BreakEventsModel : public QAbstractTableModel
{
public:
    enum Column { EventColumn, ParameterColumn, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    QStringList breakEvents() const
    {
        // "eh" for an event that is simply on, "out:Needle" for one with a
        // filter. An empty filter is left off rather than written as a
        // trailing colon.
        QStringList events;
        for (int e = 0; e < eventCount(); ++e) {
            if (!m_enabled.at(e))
                continue;
            QString event = QLatin1String(eventDescriptions[e].abbreviation);
            if (!m_parameters.at(e).isEmpty())
                event += ':' + m_parameters.at(e);
            events.append(event);
        }
        return events;
    }

    void setBreakEvents(const QStringList &events)
    {
        beginResetModel();
        m_enabled.fill(false, eventCount());
        m_parameters.fill(QString(), eventCount());
        for (const QString &event : events) {
            const int colon = event.indexOf(':');
            const int index = indexOfEvent(colon != -1 ? event.left(colon) : event);
            if (index == -1)
                continue;
            m_enabled[index] = true;
            // An event that takes no parameter never gets one, whatever the
            // stored string says.
            if (colon != -1 && eventDescriptions[index].hasParameter)
                m_parameters[index] = event.mid(colon + 1);
        }
        endResetModel();
    }

    int rowCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : eventCount();
    }

    int columnCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        const int row = index.row();
        switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            if (index.column() == EventColumn)
                return Tr::tr(eventDescriptions[row].description);
            return m_parameters.at(row);
        case Qt::CheckStateRole:
            if (index.column() != EventColumn)
                return {};
            return m_enabled.at(row) ? Qt::Checked : Qt::Unchecked;
        case AspectTable::CheckableRole:
            return index.column() == EventColumn;
        case AspectTable::EditableRole:
            return AspectTable::isWritable(flags(index));
        default:
            return {};
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        const int row = index.row();
        if (role == Qt::CheckStateRole && index.column() == EventColumn) {
            m_enabled[row] = value.toInt() == Qt::Checked;
        } else if (role == Qt::EditRole && index.column() == ParameterColumn) {
            if (!eventDescriptions[row].hasParameter)
                return false;
            m_parameters[row] = value.toString();
        } else {
            return false;
        }
        emit dataChanged(index, index);
        markSettingsDirty();
        return true;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Vertical || role != Qt::DisplayRole)
            return {};
        return section == EventColumn ? Tr::tr("Break On") : Tr::tr("Parameter");
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags f = QAbstractTableModel::flags(index);
        if (index.column() == EventColumn)
            return f | Qt::ItemIsUserCheckable;
        // Only the three events that take one may have their filter typed.
        if (eventDescriptions[index.row()].hasParameter)
            return f | Qt::ItemIsEditable;
        return f & ~Qt::ItemIsEnabled;
    }

private:
    static int eventCount() { return int(sizeof(eventDescriptions) / sizeof(EventsDescription)); }

    QList<bool> m_enabled = QList<bool>(eventCount(), false);
    QStringList m_parameters = QStringList(eventCount());
};

class CdbBreakEventsAspectPrivate
{
public:
    explicit CdbBreakEventsAspectPrivate(QObject *parent)
        : m_model(parent)
    {}

    BreakEventsModel m_model;
};

CdbBreakEventsAspect::CdbBreakEventsAspect(AspectContainer *container)
    : TypedAspect(container)
    , d(new CdbBreakEventsAspectPrivate(this))
{
    setQmlName("BreakEvents");
    setLabelText(Tr::tr("Break on:"));
}

CdbBreakEventsAspect::~CdbBreakEventsAspect()
{
    delete d;
}

AspectPresentation CdbBreakEventsAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::Table;
    return p;
}

QAbstractItemModel *CdbBreakEventsAspect::tableModel()
{
    return &d->m_model;
}

bool CdbBreakEventsAspect::isDirty() const
{
    const_cast<CdbBreakEventsAspect *>(this)->guiToVolatileValue();
    return m_value != m_volatileValue;
}

bool CdbBreakEventsAspect::guiToVolatileValue()
{
    const QStringList old = m_volatileValue;
    m_volatileValue = d->m_model.breakEvents();
    return m_volatileValue != old;
}

void CdbBreakEventsAspect::volatileValueToGui()
{
    d->m_model.setBreakEvents(m_volatileValue);
}

CdbOptionsPage::CdbOptionsPage()
{
    setId("F.Debugger.Cda");
    setDisplayName(Tr::tr("CDB"));
    setCategory(Debugger::Constants::DEBUGGER_SETTINGS_CATEGORY);
    setSettingsProvider([] { return &settings().page5; });
}


// ---------- CdbPathsPage

CdbPathsPage::CdbPathsPage()
{
    setId("F.Debugger.Cdb");
    setDisplayName(Tr::tr("CDB Paths"));
    setCategory(Debugger::Constants::DEBUGGER_SETTINGS_CATEGORY);
    setSettingsProvider([] { return &settings().page6; });
}

} // namespace Debugger::Internal

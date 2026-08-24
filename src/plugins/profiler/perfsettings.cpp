// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "perfsettings.h"

#include "perfconfigeventsmodel.h"
#include "perfprofilerconstants.h"
#include "perfprofilertr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/target.h>

#include <utils/aspectpresentation.h>
#include <utils/aspects.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcprocess.h>
#include <utils/qtcassert.h>
#include <utils/widgets.h>

#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QStyledItemDelegate>
#include <QTableView>

using namespace ProjectExplorer;
using namespace Utils;

using namespace Profiler::Internal;

namespace Profiler {

class PerfConfigWidget : public QWidget
{
public:
    PerfConfigWidget(PerfSettings *settings, Target *target);

private:
    void readTracePoints();
    void handleProcessDone();

    PerfSettings *m_settings;
    std::unique_ptr<Utils::Process> m_process;

    QTableView *eventsView;
    QPushButton *useTracePointsButton;
};

class SettingsDelegate : public QStyledItemDelegate
{
public:
    SettingsDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &) const override
    {
        editor->setGeometry(option.rect);
    }
};

PerfConfigWidget::PerfConfigWidget(PerfSettings *settings, Target *target)
    : m_settings(settings)
{
    eventsView = new QTableView(this);
    eventsView->setMinimumSize(QSize(0, 300));
    eventsView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    eventsView->setSelectionMode(QAbstractItemView::SingleSelection);
    eventsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    eventsView->setModel(m_settings->events.tableModel());
    eventsView->setItemDelegate(new SettingsDelegate(this));
    eventsView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    useTracePointsButton = new QPushButton(this);
    useTracePointsButton->setText(Tr::tr("Use Trace Points"));
    useTracePointsButton->setVisible(target != nullptr);
    connect(useTracePointsButton, &QPushButton::pressed,
            this, &PerfConfigWidget::readTracePoints);

    auto addEventButton = new QPushButton(Tr::tr("Add Event"), this);
    connect(addEventButton, &QPushButton::pressed, this, [this] {
        auto model = eventsView->model();
        model->insertRow(model->rowCount());
        markSettingsDirty();
    });

    auto removeEventButton = new QPushButton(Tr::tr("Remove Event"), this);
    connect(removeEventButton, &QPushButton::pressed, this, [this] {
        QModelIndex index = eventsView->currentIndex();
        if (!index.isValid())
            return;
        eventsView->model()->removeRow(index.row());
        markSettingsDirty();
    });

    auto resetButton = new QPushButton(Tr::tr("Reset"), this);
    connect(resetButton, &QPushButton::pressed, m_settings, &PerfSettings::reset);

    using namespace Layouting;
    Column {
        Row { st, useTracePointsButton, addEventButton, removeEventButton, resetButton },

        eventsView,

        Grid {
            m_settings->callgraphMode, m_settings->stackSize, br,
            m_settings->sampleMode, m_settings->period, br,
            m_settings->extraArguments,
        },

        st
    }.attachTo(this);

    IDevice::ConstPtr device;
    if (target)
        device = RunDeviceKitAspect::device(target->kit());

    if (!device) {
        useTracePointsButton->setEnabled(false);
        return;
    }

    QTC_ASSERT(device, return);
    QTC_CHECK(!m_process || m_process->state() == ProcessState::NotRunning);

    m_process.reset(new Process);
    m_process->setCommand({device->filePath("perf"), {"probe", "-l"}});
    connect(m_process.get(), &Process::done,
            this, &PerfConfigWidget::handleProcessDone);

    useTracePointsButton->setEnabled(true);
}

void PerfConfigWidget::readTracePoints()
{
    QMessageBox messageBox;
    messageBox.setWindowTitle(Tr::tr("Use Trace Points"));
    messageBox.setIcon(QMessageBox::Question);
    messageBox.setText(Tr::tr("Replace events with trace points read from the device?"));
    messageBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    if (messageBox.exec() == QMessageBox::Yes) {
        m_process->start();
        useTracePointsButton->setEnabled(false);
    }
}

void PerfConfigWidget::handleProcessDone()
{
    if (m_process->error() == ProcessError::FailedToStart) {
        Utils::AsynchronousMessageBox::warning(
                    Tr::tr("Cannot List Trace Points"),
                    Tr::tr("\"perf probe -l\" failed to start. Is perf installed?"));
        useTracePointsButton->setEnabled(true);
        return;
    }
    const QList<QByteArray> lines
        = m_process->rawStdOut().append(m_process->rawStdErr()).split('\n');
    auto model = eventsView->model();
    const int previousRows = model->rowCount();
    QHash<QByteArray, QByteArray> tracePoints;
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        const int space = trimmed.indexOf(' ');
        if (space < 0)
            continue;

        // If the whole "on ..." string is the same, the trace points are redundant
        tracePoints[trimmed.mid(space + 1)] = trimmed.left(space);
    }

    if (tracePoints.isEmpty()) {
        Utils::AsynchronousMessageBox::warning(
                    Tr::tr("No Trace Points Found"),
                    Tr::tr("Trace points can be defined with \"perf probe -a\"."));
    } else {
        for (const QByteArray &event : std::as_const(tracePoints)) {
            int row = model->rowCount();
            model->insertRow(row);
            model->setData(model->index(row, PerfConfigEventsModel::ColumnEventType),
                           PerfConfigEventsModel::EventTypeCustom);
            model->setData(model->index(row, PerfConfigEventsModel::ColumnSubType),
                           QString::fromUtf8(event));
        }
        model->removeRows(0, previousRows);
        m_settings->sampleMode.setVolatileValue(1);
        m_settings->period.setVolatileValue(1);
    }
    useTracePointsButton->setEnabled(true);
}

QWidget *SettingsDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                        const QModelIndex &index) const
{
    Q_UNUSED(option)
    if (!index.flags().testFlag(Qt::ItemIsEditable))
        return nullptr;

    const QVariantList choices = index.data(AspectTable::ChoicesRole).toList();
    if (!choices.isEmpty()) {
        QComboBox *editor = new QComboBox(parent);
        for (const QVariant &choice : choices) {
            const QVariantMap map = choice.toMap();
            editor->addItem(map.value("display").toString(), map.value("id"));
        }
        return editor;
    }

    QLineEdit *editor = new QLineEdit(parent);
    const QString validator = index.data(AspectTable::ValidatorRole).toString();
    if (!validator.isEmpty()) {
        editor->setValidator(
            new QRegularExpressionValidator(QRegularExpression(validator), editor));
    }
    return editor;
}

void SettingsDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if (QComboBox *combo = qobject_cast<QComboBox *>(editor)) {
        QVariant data = index.model()->data(index, Qt::EditRole);
        for (int i = 0, end = combo->count(); i != end; ++i) {
            if (combo->itemData(i) == data) {
                combo->setCurrentIndex(i);
                return;
            }
        }
    } else if (QLineEdit *lineedit = qobject_cast<QLineEdit *>(editor)) {
        lineedit->setText(index.model()->data(index, Qt::DisplayRole).toString());
    }
}

void SettingsDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                    const QModelIndex &index) const
{
    if (QComboBox *combo = qobject_cast<QComboBox *>(editor)) {
        model->setData(index, combo->currentData());
    } else if (QLineEdit *lineedit = qobject_cast<QLineEdit *>(editor)) {
        model->setData(index, lineedit->text());
    }
}

// PerfSettingsPage

AspectPresentation PerfEventsAspect::presentation() const
{
    AspectPresentation p = StringListAspect::presentation();
    p.control = AspectControls::Table;
    return p;
}

PerfSettings &globalSettings()
{
    static PerfSettings theSettings(nullptr);
    return theSettings;
}

PerfSettings::PerfSettings(ProjectExplorer::Target *target)
{
    Q_UNUSED(target)
    setAutoApply(false);
    setId(Constants::PerfSettingsId);

    period.setSettingsKey("Analyzer.Perf.Frequency");
    period.setRange(250, 2147483647);
    period.setDefaultValue(250);
    period.setLabelText(Tr::tr("Sample period:"));

    stackSize.setSettingsKey("Analyzer.Perf.StackSize");
    stackSize.setRange(4096, 65536);
    stackSize.setDefaultValue(4096);
    stackSize.setLabelText(Tr::tr("Stack snapshot size (kB):"));

    sampleMode.setSettingsKey("Analyzer.Perf.SampleMode");
    sampleMode.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    sampleMode.setLabelText(Tr::tr("Sample mode:"));
    sampleMode.addOption({Tr::tr("frequency (Hz)"), {}, QString("-F")});
    sampleMode.addOption({Tr::tr("event count"), {}, QString("-c")});
    sampleMode.setDefaultValue(0);

    callgraphMode.setSettingsKey("Analyzer.Perf.CallgraphMode");
    callgraphMode.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    callgraphMode.setLabelText(Tr::tr("Call graph mode:"));
    callgraphMode.addOption({Tr::tr("dwarf"), {}, QString(Constants::PerfCallgraphDwarf)});
    callgraphMode.addOption({Tr::tr("frame pointer"), {}, QString("fp")});
    callgraphMode.addOption({Tr::tr("last branch record"), {}, QString("lbr")});
    callgraphMode.setDefaultValue(0);

    events.setSettingsKey("Analyzer.Perf.Events");
    events.setDefaultValue({"cpu-cycles"});
    events.setLabelText(Tr::tr("Events:"));
    events.setTableModel(new Internal::PerfConfigEventsModel(this, this));

    extraArguments.setSettingsKey("Analyzer.Perf.ExtraArguments");
    extraArguments.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);
    extraArguments.setLabelText(Tr::tr("Additional arguments:"));
    extraArguments.setSpan(4);

    resetToDefaults.setQmlName("ResetToDefaults");
    resetToDefaults.setActionText(Tr::tr("Reset"));
    resetToDefaults.setAction([this] { reset(); });

    connect(&callgraphMode, &SelectionAspect::volatileValueChanged, this, [this] {
        stackSize.setEnabled(callgraphMode.volatileValue() == 0);
    });

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Profiler/PerfSettingsPage.qml"));

    readGlobalSettings();
    readSettings();
}

PerfSettings::~PerfSettings()
{
}

void PerfSettings::readGlobalSettings()
{
    Store defaults;

    // Read stored values
    QtcSettings *settings = Core::ICore::settings();
    settings->beginGroup(Constants::AnalyzerSettingsGroupId);
    Store map = defaults;
    for (Store::ConstIterator it = defaults.constBegin(); it != defaults.constEnd(); ++it)
        map.insert(it.key(), settings->value(it.key(), it.value()));
    settings->endGroup();

    fromMap(map);
}

void PerfSettings::writeGlobalSettings() const
{
    QtcSettings *settings = Core::ICore::settings();
    settings->beginGroup(Constants::AnalyzerSettingsGroupId);
    Store map;
    toMap(map);
    for (Store::ConstIterator it = map.constBegin(); it != map.constEnd(); ++it)
        settings->setValue(it.key(), it.value());
    settings->endGroup();
}

void PerfSettings::toMap(Store &map) const
{
    AspectContainer::toMap(map);
    map[Constants::PerfRecordArgsId] = perfRecordArguments();
}

QString PerfSettings::perfRecordArguments() const
{
    QString callgraphArg = callgraphMode.itemValue().toString();
    if (callgraphArg == Constants::PerfCallgraphDwarf)
        callgraphArg += "," + QString::number(stackSize());

    QString events;
    for (const QString &event : this->events()) {
        if (!event.isEmpty()) {
            if (!events.isEmpty())
                events.append(',');
            events.append(event);
        }
    }

    CommandLine cmd;
    cmd.addArgs({"-e", events,
                 "--call-graph", callgraphArg,
                 sampleMode.itemValue().toString(),
                 QString::number(period())});
    cmd.addArgs(extraArguments(), CommandLine::Raw);
    return cmd.arguments();
}

QWidget *PerfSettings::createPerfConfigWidget(Target *target)
{
    return new PerfConfigWidget(this, target);
}

// PerfSettingsPage

class PerfSettingsPage final : public Core::IOptionsPage
{
public:
    PerfSettingsPage()
    {
        setId(Constants::PerfSettingsId);
        setDisplayName(Tr::tr("CPU Usage"));
        setCategory("T.Analyzer");
        setSettingsProvider([] { return &globalSettings(); });
    }
};

const PerfSettingsPage settingsPage;

} // namespace Profiler

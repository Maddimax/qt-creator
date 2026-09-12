// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputpanemanager.h"

#include "actionmanager/actioncontainer.h"
#include "actionmanager/actionmanager.h"
#include "actionmanager/command.h"
#include "coreplugintr.h"
#include "editormanager/editormanager.h"
#include "editormanager/ieditor.h"
#include "find/optionspopup.h"
#include "findplaceholder.h"
#include "icore.h"
#include "ioutputpane.h"
#include "modemanager.h"
#include "inavigationwidgetfactory.h"
#include "outputpane.h"
#include "statusbarmanager.h"

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/proxyaction.h>
#include <utils/qtcassert.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>
#include <utils/widgets.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QFocusEvent>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPainter>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimeLine>
#include <QToolBar>
#include <QToolButton>

#ifdef WITH_TESTS
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

#include <limits>
#endif

using namespace Utils;
using namespace Core::Internal;

namespace Core {
namespace Internal {

Q_GLOBAL_STATIC(QList<Core::OutputPanePlaceHolder *>, sPlaceholders)

class BadgeLabel
{
public:
    BadgeLabel();
    void paint(QPainter *p, int x, int y, bool isChecked);
    void setText(const QString &text);
    QString text() const;
    QSize sizeHint() const;

private:
    void calculateSize();

    QSize m_size;
    QString m_text;
    QFont m_font;
    static const int m_padding = 6;
};

class OutputPaneToggleButton : public QToolButton
{
    Q_OBJECT

public:
    OutputPaneToggleButton(int number, const QString &text, QAction *action);

    QSize sizeHint() const override;
    void paintEvent(QPaintEvent*) override;
    void flash(int count = 3);
    void setIconBadgeNumber(int number);

    void contextMenuEvent(QContextMenuEvent *e) override;

signals:
    void contextMenuRequested();

private:
    void updateToolTip();
    void checkStateSet() override;

    QString m_number;
    QString m_text;
    QAction *m_action;
    QTimeLine *m_flashTimer;
    BadgeLabel m_badgeNumberLabel;
};

class OutputPaneManageButton : public QToolButton
{
    Q_OBJECT
public:
    OutputPaneManageButton();
    void paintEvent(QPaintEvent *) override;

    void contextMenuEvent(QContextMenuEvent *e) override;

signals:
    void menuRequested();
};

} // Internal

class OutputPanePlaceHolderPrivate
{
public:
    OutputPanePlaceHolderPrivate(Id mode, QSplitter *parent)
        : m_mode(mode), m_splitter(parent)
    {}

    Id m_mode;
    QSplitter *m_splitter;
    int m_nonMaximizedSize = 0;
    bool m_isMaximized = false;
    bool m_initialized = false;
    static OutputPanePlaceHolder* m_current;
};

OutputPanePlaceHolder *OutputPanePlaceHolderPrivate::m_current = nullptr;

OutputPanePlaceHolder::OutputPanePlaceHolder(Id mode, QSplitter *parent)
   : QWidget(parent), d(new OutputPanePlaceHolderPrivate(mode, parent))
{
    sPlaceholders->append(this);
    setVisible(false);
    setLayout(new QVBoxLayout);
    QSizePolicy sp;
    sp.setHorizontalPolicy(QSizePolicy::Preferred);
    sp.setVerticalPolicy(QSizePolicy::Preferred);
    sp.setHorizontalStretch(0);
    setSizePolicy(sp);
    layout()->setContentsMargins(0, 0, 0, 0);
    connect(ModeManager::instance(), &ModeManager::currentModeChanged,
            this, &OutputPanePlaceHolder::currentModeChanged);
    // if this is part of a lazily created mode widget,
    // we need to check if this is the current placeholder
    currentModeChanged(ModeManager::currentModeId());
}

OutputPanePlaceHolder::~OutputPanePlaceHolder()
{
    if (OutputPanePlaceHolderPrivate::m_current == this) {
        if (Internal::OutputPaneManager *om = Internal::OutputPaneManager::instance()) {
            om->setParent(nullptr);
            om->hide();
        }
        OutputPanePlaceHolderPrivate::m_current = nullptr;
    }
    delete d;
}

void OutputPanePlaceHolder::currentModeChanged(Id mode)
{
    if (OutputPanePlaceHolderPrivate::m_current == this) {
        OutputPanePlaceHolderPrivate::m_current = nullptr;
        if (d->m_initialized)
            Internal::OutputPaneManager::setOutputPaneHeightSetting(d->m_nonMaximizedSize);
        Internal::OutputPaneManager *om = Internal::OutputPaneManager::instance();
        om->hide();
        om->setParent(nullptr);
        om->updateStatusButtons(false);
    }
    if (d->m_mode == mode) {
        if (OutputPanePlaceHolderPrivate::m_current && OutputPanePlaceHolderPrivate::m_current->d->m_initialized)
            Internal::OutputPaneManager::setOutputPaneHeightSetting(OutputPanePlaceHolderPrivate::m_current->d->m_nonMaximizedSize);
        Core::OutputPanePlaceHolderPrivate::m_current = this;
        Internal::OutputPaneManager *om = Internal::OutputPaneManager::instance();
        layout()->addWidget(om);
        om->show();
        om->updateStatusButtons(isVisible());
        Internal::OutputPaneManager::updateMaximizeButton(d->m_isMaximized);
    }
}

void OutputPanePlaceHolder::setMaximized(bool maximize)
{
    if (d->m_isMaximized == maximize)
        return;
    if (!d->m_splitter)
        return;
    int idx = d->m_splitter->indexOf(this);
    if (idx < 0)
        return;

    d->m_isMaximized = maximize;
    if (OutputPanePlaceHolderPrivate::m_current == this)
        Internal::OutputPaneManager::updateMaximizeButton(d->m_isMaximized);
    QList<int> sizes = d->m_splitter->sizes();

    if (maximize) {
        d->m_nonMaximizedSize = sizes[idx];
        int sum = 0;
        for (const int s : std::as_const(sizes))
            sum += s;
        for (int i = 0; i < sizes.count(); ++i) {
            sizes[i] = 32;
        }
        sizes[idx] = sum - (sizes.count()-1) * 32;
    } else {
        int target = d->m_nonMaximizedSize > 0 ? d->m_nonMaximizedSize : sizeHint().height();
        int space = sizes[idx] - target;
        if (space > 0) {
            for (int i = 0; i < sizes.count(); ++i) {
                sizes[i] += space / (sizes.count()-1);
            }
            sizes[idx] = target;
        }
    }

    d->m_splitter->setSizes(sizes);
}

bool OutputPanePlaceHolder::isMaximized() const
{
    return d->m_isMaximized;
}

void OutputPanePlaceHolder::setHeight(int height)
{
    if (height == 0)
        return;
    if (!d->m_splitter)
        return;
    const int idx = d->m_splitter->indexOf(this);
    if (idx < 0)
        return;

    d->m_splitter->refresh();
    QList<int> sizes = d->m_splitter->sizes();
    const int difference = height - sizes.at(idx);
    if (difference == 0)
        return;
    const int adaption = difference / (sizes.count()-1);
    for (int i = 0; i < sizes.count(); ++i) {
        sizes[i] -= adaption;
    }
    sizes[idx] = height;
    d->m_splitter->setSizes(sizes);
}

void OutputPanePlaceHolder::ensureSizeHintAsMinimum()
{
    if (!d->m_splitter)
        return;
    Internal::OutputPaneManager *om = Internal::OutputPaneManager::instance();
    int minimum = (d->m_splitter->orientation() == Qt::Vertical
                   ? om->sizeHint().height() : om->sizeHint().width());
    if (nonMaximizedSize() < minimum && !d->m_isMaximized)
        setHeight(minimum);
}

int OutputPanePlaceHolder::nonMaximizedSize() const
{
    if (!d->m_initialized)
        return Internal::OutputPaneManager::outputPaneHeightSetting();
    return d->m_nonMaximizedSize;
}

Id OutputPanePlaceHolder::mode() const
{
    return d->m_mode;
}

void OutputPanePlaceHolder::resizeEvent(QResizeEvent *event)
{
    if (d->m_isMaximized || event->size().height() == 0)
        return;
    d->m_nonMaximizedSize = event->size().height();
}

void OutputPanePlaceHolder::showEvent(QShowEvent *)
{
    if (!d->m_initialized) {
        d->m_initialized = true;
        setHeight(Internal::OutputPaneManager::outputPaneHeightSetting());
    }
    if (OutputPanePlaceHolderPrivate::m_current == this) {
        Internal::OutputPaneManager *om = Internal::OutputPaneManager::instance();
        om->updateStatusButtons(true);
    }
}

OutputPanePlaceHolder *OutputPanePlaceHolder::getCurrent()
{
    return OutputPanePlaceHolderPrivate::m_current;
}

bool OutputPanePlaceHolder::isCurrentVisible()
{
    return OutputPanePlaceHolderPrivate::m_current && OutputPanePlaceHolderPrivate::m_current->isVisible();
}

bool OutputPanePlaceHolder::modeHasOutputPanePlaceholder(Utils::Id mode)
{
    return Utils::anyOf(*sPlaceholders, Utils::equal(&OutputPanePlaceHolder::mode, mode));
}

class OutputPaneData
{
public:
    OutputPaneData(IOutputPane *pane = nullptr) : pane(pane) {}

    IOutputPane *pane = nullptr;
    Id id;
    OutputPaneToggleButton *button = nullptr;
    QAction *action = nullptr;
    // Built once, and the flag that says this pane has been set up before.
    QPointer<QWidget> toolBar;
    // The command's own action, not the context one: it is the one whose
    // tooltip says which keys reach the pane.
    QPointer<QAction> commandAction;

    // What the row says about this pane. The button draws it; it does not own
    // it - see OutputPaneButtonModel.
    int number = 0;
    int badge = 0;
    bool checked = false;
    bool buttonVisible = false;
};

static QVector<OutputPaneData> g_outputPanes;
static bool g_managerConstructed = false; // For debugging reasons.

QWidget *createOutputPaneButtonRow()
{
    return Internal::OutputPaneManager::createButtonRow();
}

// OutputPane

IOutputPane::IOutputPane(QObject *parent)
    : QObject(parent)
{
    // We need all pages first. Ignore latecomers and shout.
    QTC_ASSERT(!g_managerConstructed, return);
    g_outputPanes.append(OutputPaneData(this));

    // reinitialize the output pane buttons if a lazy loaded plugin adds a pane
    if (OutputPaneManager::initialized())
        QMetaObject::invokeMethod(this, &OutputPaneManager::setupButtons, Qt::QueuedConnection);
}

const QList<IOutputPane *> IOutputPane::allOutputPanes()
{
    return Utils::transform<QList>(g_outputPanes, &OutputPaneData::pane);
}

IOutputPane::~IOutputPane()
{
    const int i = Utils::indexOf(g_outputPanes, Utils::equal(&OutputPaneData::pane, this));
    QTC_ASSERT(i >= 0, return);
    delete g_outputPanes.at(i).button;
    g_outputPanes.removeAt(i);

}

QList<QWidget *> IOutputPane::toolBarWidgets() const
{
    QList<QWidget *> widgets;
    if (m_filterOutputLineEdit)
        widgets << m_filterOutputLineEdit;
    return widgets;
}

QList<Id> IOutputPane::toolBarCommands() const
{
    return {Constants::ZOOM_IN, Constants::ZOOM_OUT};
}

QList<Utils::BaseAspect *> IOutputPane::toolBarAspects() const
{
    return {};
}
QList<IOutputPane::ToolBarItem> IOutputPane::toolBarItems() const
{
    QList<ToolBarItem> items;
    for (QWidget * const widget : toolBarWidgets())
        items << ToolBarItem::forWidget(widget);
    for (Utils::BaseAspect * const aspect : toolBarAspects())
        items << ToolBarItem::forAspect(aspect);
    for (const Id command : toolBarCommands())
        items << ToolBarItem::forCommand(command);
    return items;
}


/*!
    Returns the ID of the output pane.
*/
Id IOutputPane::id() const
{
    return m_id;
}

/*!
    Sets the ID of the output pane to \a id.
    This is used for persisting the visibility state.
*/
void IOutputPane::setId(const Utils::Id &id)
{
    m_id = id;
}

/*!
    Returns the translated display name of the output pane.
*/
QString IOutputPane::displayName() const
{
    return m_displayName;
}

/*!
    Determines the position of the output pane on the status bar and the
    default visibility.
    \sa setPriorityInStatusBar()
*/
int IOutputPane::priorityInStatusBar() const
{
    return m_priority;
}

/*!
    Sets the position of the output pane on the status bar and the default
    visibility to \a priority.
    \list
        \li higher numbers are further to the front
        \li >= 0 are shown in status bar by default
        \li < 0 are not shown in status bar by default
    \endlist
*/
void IOutputPane::setPriorityInStatusBar(int priority)
{
    m_priority = priority;
}

/*!
    Sets the translated display name of the output pane to \a name.
*/
void IOutputPane::setDisplayName(const QString &name)
{
    m_displayName = name;
}

void IOutputPane::visibilityChanged(bool /*visible*/)
{
}

bool IOutputPane::hasFilterContext() const
{
    return false;
}

void IOutputPane::setFont(const QFont &font)
{
    emit fontChanged(font);
}

void IOutputPane::setWheelZoomEnabled(bool enabled)
{
    emit wheelZoomEnabledChanged(enabled);
}

void IOutputPane::setupFilterUi(const Key &historyKey, const QString &actionSuffix)
{
    m_filterActionSuffix = actionSuffix;

    ActionBuilder filterRegexpAction(this, filterRegexpActionId());
    filterRegexpAction.setText(Tr::tr("Use Regular Expressions"));
    filterRegexpAction.setCheckable(true);
    filterRegexpAction.addOnToggled(this, &IOutputPane::setRegularExpressions);

    ActionBuilder filterCaseSensitiveAction(this, filterCaseSensitivityActionId());
    filterCaseSensitiveAction.setText(Tr::tr("Case Sensitive"));
    filterCaseSensitiveAction.setCheckable(true);
    filterCaseSensitiveAction.addOnToggled(this, &IOutputPane::setCaseSensitive);

    ActionBuilder invertFilterAction(this, filterInvertedActionId());
    invertFilterAction.setText(Tr::tr("Show Non-matching Lines"));
    invertFilterAction.setCheckable(true);
    invertFilterAction.addOnToggled(this, [this, action=invertFilterAction.contextAction()] {
        m_invertFilter = action->isChecked();
        updateFilter();
    });

    ActionBuilder filterBeforeAction(this, filterBeforeActionId());
    //: The placeholder "{}" is replaced by a spin box for selecting a number.
    filterBeforeAction.setText(Tr::tr("Show {} &preceding lines"));
    QAction *action = filterBeforeAction.contextAction();
    NumericOption::set(action, NumericOption{0, 0, 9});
    NumericOption::set(filterBeforeAction.commandAction(), NumericOption{0, 0, 9});
    connect(action, &QAction::changed, this, [this, action] {
        const std::optional<NumericOption> option = NumericOption::get(action);
        QTC_ASSERT(option, return);
        m_beforeContext = option->currentValue;
        updateFilter();
    });

    ActionBuilder filterAfterAction(this, filterAfterActionId());
    //: The placeholder "{}" is replaced by a spin box for selecting a number.
    filterAfterAction.setText(Tr::tr("Show {} &subsequent lines"));
    action = filterAfterAction.contextAction();
    NumericOption::set(action, NumericOption{0, 0, 9});
    NumericOption::set(filterAfterAction.commandAction(), NumericOption{0, 0, 9});
    connect(action, &QAction::changed, this, [this, action] {
        const std::optional<NumericOption> option = NumericOption::get(action);
        QTC_ASSERT(option, return);
        m_afterContext = option->currentValue;
        updateFilter();
    });

    m_filterOutputLineEdit = new FancyLineEdit;
    m_filterOutputLineEdit->setPlaceholderText(Tr::tr("Filter output..."));
    m_filterOutputLineEdit->setButtonVisible(FancyLineEdit::Left, true);
    m_filterOutputLineEdit->setButtonIcon(FancyLineEdit::Left, Icons::MAGNIFIER.icon());
    m_filterOutputLineEdit->setFiltering(true);
    m_filterOutputLineEdit->setEnabled(false);
    m_filterOutputLineEdit->setHistoryCompleter(historyKey);
    m_filterOutputLineEdit->setAttribute(Qt::WA_MacShowFocusRect, false);
    connect(m_filterOutputLineEdit, &FancyLineEdit::textChanged,
            this, &IOutputPane::updateFilter);
    connect(m_filterOutputLineEdit, &FancyLineEdit::returnPressed,
            this, &IOutputPane::updateFilter);
    connect(m_filterOutputLineEdit, &FancyLineEdit::leftButtonClicked,
            this, &IOutputPane::filterOutputButtonClicked);
}

QString IOutputPane::filterText() const
{
    return m_filterOutputLineEdit->text();
}

void IOutputPane::setFilteringEnabled(bool enable)
{
    m_filterOutputLineEdit->setEnabled(enable);
}

void IOutputPane::setupContext(const Id &context, QWidget *widget)
{
    return setupContext(Context(context), widget);
}

void IOutputPane::setupContext(const Context &context, QWidget *widget)
{
    IContext::attach(widget, context);

    ActionBuilder(this, Constants::ZOOM_IN)
        .setContext(context)
        .addOnTriggered(this, [this] { emit zoomInRequested(1); });

    ActionBuilder(this, Constants::ZOOM_OUT)
        .setContext(context)
        .addOnTriggered(this, [this] { emit zoomOutRequested(1); });

    ActionBuilder(this, Constants::ZOOM_RESET)
        .setContext(context)
        .addOnTriggered(this, &IOutputPane::resetZoomRequested);
}

void IOutputPane::setZoomButtonsEnabled(bool enabled)
{
    if (m_zoomEnabled == enabled)
        return;
    m_zoomEnabled = enabled;
    emit zoomEnabledChanged(m_zoomEnabled);
}

void IOutputPane::updateFilter()
{
    QTC_ASSERT(false, qDebug() << "updateFilter() needs to get re-implemented");
}

void IOutputPane::filterOutputButtonClicked()
{
    QVector<Utils::Id> commands = {filterRegexpActionId(),
                                   filterCaseSensitivityActionId(),
                                   filterInvertedActionId()};

    if (hasFilterContext()) {
        commands.emplaceBack(filterBeforeActionId());
        commands.emplaceBack(filterAfterActionId());
    }

    auto popup = new Core::OptionsPopup(m_filterOutputLineEdit, commands);
    popup->show();
}

void IOutputPane::setRegularExpressions(bool regularExpressions)
{
    m_filterRegexp = regularExpressions;
    updateFilter();
}

Id IOutputPane::filterRegexpActionId() const
{
    return Id("OutputFilter.RegularExpressions").withSuffix(m_filterActionSuffix);
}

Id IOutputPane::filterCaseSensitivityActionId() const
{
    return Id("OutputFilter.CaseSensitive").withSuffix(m_filterActionSuffix);
}

Id IOutputPane::filterInvertedActionId() const
{
    return Id("OutputFilter.Invert").withSuffix(m_filterActionSuffix);
}

Id IOutputPane::filterBeforeActionId() const
{
    return Id("OutputFilter.BeforeContext").withSuffix(m_filterActionSuffix);
}

Id IOutputPane::filterAfterActionId() const
{
    return Id("OutputFilter.AfterContext").withSuffix(m_filterActionSuffix);
}

void IOutputPane::setCaseSensitive(bool caseSensitive)
{
    m_filterCaseSensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    updateFilter();
}

namespace Internal {

// OutputPaneButtonModel

OutputPaneButtonModel::OutputPaneButtonModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int OutputPaneButtonModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return g_outputPanes.size();
}

QVariant OutputPaneButtonModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= g_outputPanes.size())
        return {};

    const OutputPaneData &data = g_outputPanes.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return data.pane->displayName();
    case Qt::ToolTipRole:
        return data.commandAction ? data.commandAction->toolTip() : QString();
    case NumberRole:
        return data.number;
    case BadgeRole:
        return data.badge ? QString::number(data.badge) : QString();
    case CheckedRole:
        return data.checked;
    case ButtonVisibleRole:
        return data.buttonVisible;
    default:
        return {};
    }
}

QHash<int, QByteArray> OutputPaneButtonModel::roleNames() const
{
    QHash<int, QByteArray> names = QAbstractListModel::roleNames();
    names[NumberRole] = "number";
    names[BadgeRole] = "badge";
    names[CheckedRole] = "checked";
    names[ButtonVisibleRole] = "buttonVisible";
    return names;
}

bool OutputPaneButtonModel::isButtonVisible(int row) const
{
    QTC_ASSERT(row >= 0 && row < g_outputPanes.size(), return false);
    return g_outputPanes.at(row).buttonVisible;
}

void OutputPaneButtonModel::setButtonVisible(int row, bool visible)
{
    QTC_ASSERT(row >= 0 && row < g_outputPanes.size(), return);
    if (g_outputPanes.at(row).buttonVisible == visible)
        return;
    g_outputPanes[row].buttonVisible = visible;
    emit dataChanged(index(row), index(row), {ButtonVisibleRole});
}

void OutputPaneButtonModel::setChecked(int row, bool checked)
{
    QTC_ASSERT(row >= 0 && row < g_outputPanes.size(), return);
    if (g_outputPanes.at(row).checked == checked)
        return;
    g_outputPanes[row].checked = checked;
    emit dataChanged(index(row), index(row), {CheckedRole});
}

void OutputPaneButtonModel::setBadge(int row, int number)
{
    QTC_ASSERT(row >= 0 && row < g_outputPanes.size(), return);
    if (g_outputPanes.at(row).badge == number)
        return;
    g_outputPanes[row].badge = number;
    emit dataChanged(index(row), index(row), {BadgeRole});
}

void OutputPaneButtonModel::toolTipChanged(int row)
{
    QTC_ASSERT(row >= 0 && row < g_outputPanes.size(), return);
    emit dataChanged(index(row), index(row), {Qt::ToolTipRole});
}

void OutputPaneButtonModel::requestFlash(int row)
{
    QTC_ASSERT(row >= 0 && row < g_outputPanes.size(), return);
    emit flashRequested(row);
}

void OutputPaneButtonModel::activate(int row)
{
    QTC_ASSERT(row >= 0 && row < g_outputPanes.size(), return);
    OutputPaneManager::instance()->buttonTriggered(row);
}

void OutputPaneButtonModel::showMenu()
{
    OutputPaneManager::instance()->popupMenu();
}

void OutputPaneButtonModel::reset()
{
    // Everything about every row: the panes have just been sorted and
    // renumbered.
    beginResetModel();
    endResetModel();
}

// OutputPaneButtons

// The Qt Quick row draws the buttons, and the arrow that manages them, from
// the model above. QTC_WIDGET_OUTPUT_BUTTONS asks for the QToolButtons
// instead, and a build with no front end to host QML gets them without asking.
static bool useQuickButtonRow()
{
    return !Utils::qtcEnvironmentVariableIsSet("QTC_WIDGET_OUTPUT_BUTTONS")
           && hasQmlViewFactory();
}

QWidget *OutputPaneManager::createButtonRow()
{
    OutputPaneManager * const manager = instance();
    QTC_ASSERT(manager, return nullptr);
    auto * const buttons = new OutputPaneButtons(manager->m_buttonModel);
    if (QWidget * const row = createQmlView(
            QUrl("qrc:/qt/qml/QtCreator/Core/OutputPaneButtons.qml"), buttons,
            QmlViewSizing::SizeToScene)) {
        return row;
    }
    delete buttons;
    return nullptr;
}

OutputPaneButtons::OutputPaneButtons(OutputPaneButtonModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    connect(m_model, &OutputPaneButtonModel::flashRequested,
            this, &OutputPaneButtons::flashRequested);
}

QAbstractItemModel *OutputPaneButtons::model() const
{
    return m_model;
}

void OutputPaneButtons::activate(int row)
{
    m_model->activate(row);
}

void OutputPaneButtons::showMenu()
{
    m_model->showMenu();
}


const char outputPaneSettingsKeyC[] = "OutputPaneVisibility";
const char outputPaneIdKeyC[] = "id";
const char outputPaneVisibleKeyC[] = "visible";
const int buttonBorderWidth = 3;

static int numberAreaWidth()
{
    return creatorTheme()->flag(Theme::FlatToolBars) ? 15 : 19;
}

////
// OutputPaneManager
////

static OutputPaneManager *m_instance = nullptr;

void OutputPaneManager::create()
{
   m_instance = new OutputPaneManager;
}

void OutputPaneManager::destroy()
{
    delete m_instance;
    m_instance = nullptr;
}

OutputPaneManager *OutputPaneManager::instance()
{
    return m_instance;
}

void OutputPaneManager::updateStatusButtons(bool visible)
{
    int idx = currentIndex();
    if (idx == -1)
        return;
    QTC_ASSERT(idx < g_outputPanes.size(), return);
    const OutputPaneData &data = g_outputPanes.at(idx);
    m_buttonModel->setChecked(idx, visible);
    data.pane->visibilityChanged(visible);
}

void OutputPaneManager::updateMaximizeButton(bool maximized)
{
    if (maximized) {
        m_instance->m_minMaxAction->setIcon(Utils::Icons::ARROW_DOWN.icon());
        m_instance->m_minMaxAction->setText(Tr::tr("Minimize"));
    } else {
        m_instance->m_minMaxAction->setIcon(Utils::Icons::ARROW_UP.icon());
        m_instance->m_minMaxAction->setText(Tr::tr("Maximize"));
    }
}

// Return shortcut as Alt+<number> or Cmd+<number> if number is a non-zero digit
static QKeySequence paneShortCut(int number)
{
    if (number < 1 || number > 9)
        return QKeySequence();

    const int modifier = HostOsInfo::isMacHost() ? Qt::CTRL : Qt::ALT;
    return QKeySequence(modifier | (Qt::Key_0 + number));
}

OutputPaneManager::OutputPaneManager(QWidget *parent) :
    QWidget(parent),
    m_buttonModel(new OutputPaneButtonModel(this)),
    m_titleLabel(new QLabel),
    m_outputWidgetPane(new QStackedWidget),
    m_opToolBarWidgets(new QStackedWidget)
{
    setWindowTitle(Tr::tr("Output"));

    m_titleLabel->setContentsMargins(5, 0, 5, 0);

    connect(ICore::instance(), &ICore::saveSettingsRequested, this, &OutputPaneManager::saveSettings);

    // The buttons are a view of the model: what a row says about itself
    // reaches the button here and nowhere else. There may be no button yet -
    // a pane can report a badge while the shell is still starting - which is
    // what the guard is for.
    connect(m_buttonModel, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
        for (int row = topLeft.row(); row <= bottomRight.row(); ++row)
            updateButton(row);
    });
    connect(m_buttonModel, &OutputPaneButtonModel::flashRequested, this, [](int row) {
        if (OutputPaneToggleButton * const button = g_outputPanes.at(row).button)
            button->flash();
    });

    auto toolBar = new StyledBar;
    auto clearButton = new QToolButton;
    auto prevToolButton = new QToolButton;
    auto nextToolButton = new QToolButton;
    auto minMaxButton = new QToolButton;
    auto closeButton = new QToolButton;

    m_buttonsWidget = new QWidget;
    m_buttonsWidget->setObjectName("OutputPaneButtons"); // used for UI introduction

    using namespace Layouting;
    Row {
        m_titleLabel,
        new StyledSeparator,
        clearButton,
        prevToolButton,
        nextToolButton,
        m_opToolBarWidgets,
        minMaxButton,
        closeButton,
        spacing(0), noMargin,
    }.attachTo(toolBar);

    Column {
        toolBar,
        m_outputWidgetPane,
        new FindToolBarPlaceHolder(this),
        spacing(0), noMargin,
    }.attachTo(this);

    Row {
        spacing(creatorTheme()->flag(Theme::FlatToolBars) ? 9 : 4), customMargins(5, 0, 0, 0),
    }.attachTo(m_buttonsWidget);

    StatusBarManager::addStatusBarWidget(m_buttonsWidget, StatusBarManager::Second);

    ActionContainer *mview = ActionManager::actionContainer(Constants::M_VIEW);

    // Window->Output Panes
    ActionContainer *mpanes = ActionManager::createMenu(Constants::M_VIEW_PANES);
    mview->addMenu(mpanes, Constants::G_VIEW_PANES);
    mpanes->menu()->setTitle(Tr::tr("Out&put"));
    mpanes->appendGroup("Coreplugin.OutputPane.ActionsGroup");
    mpanes->appendGroup("Coreplugin.OutputPane.PanesGroup");

    ActionBuilder clearAction(this, Constants::OUTPUTPANE_CLEAR);
    clearAction.setIcon(Utils::Icons::CLEAN.icon())
        .setText(Tr::tr("Clear"))
        .addOnTriggered(this, &OutputPaneManager::clearPage)
        .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.ActionsGroup")
        .bindContextAction(&m_clearAction);
    clearButton->setDefaultAction(
        ProxyAction::proxyActionWithIcon(
            clearAction.contextAction(), Utils::Icons::CLEAN_TOOLBAR.icon()));

    ActionBuilder prevAction(this, "Coreplugin.OutputPane.previtem");
    prevAction
        .setIcon(Utils::Icons::ARROW_UP_TOOLBAR.icon())
        .setText(Tr::tr("Previous Item"))
        .addOnTriggered(this, &OutputPaneManager::slotPrev)
        .setDefaultKeySequence(QKeySequence(Tr::tr("Shift+F6")))
        .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.ActionsGroup")
        .bindContextAction(&m_prevAction);
    prevToolButton->setDefaultAction(prevAction.contextAction());

    ActionBuilder nextAction(this, "Coreplugin.OutputPane.nextitem");
    nextAction
        .setIcon(Utils::Icons::ARROW_DOWN_TOOLBAR.icon())
        .setText(Tr::tr("Next Item"))
        .addOnTriggered(this, &OutputPaneManager::slotNext)
        .setDefaultKeySequence(QKeySequence(Tr::tr("F6")))
        .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.ActionsGroup")
        .bindContextAction(&m_nextAction);
    nextToolButton->setDefaultAction(nextAction.contextAction());

    ActionBuilder minMaxAction(this, "Coreplugin.OutputPane.minmax");
    minMaxAction
        .setDefaultKeySequence(Tr::tr("Ctrl+Shift+9"), Tr::tr("Alt+Shift+9"))
        .setCommandAttribute(Command::CA_UpdateText)
        .setCommandAttribute(Command::CA_UpdateIcon)
        .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.ActionsGroup")
        .addOnTriggered(this, &OutputPaneManager::toggleMaximized)
        .bindContextAction(&m_minMaxAction);
    minMaxButton->setDefaultAction(minMaxAction.commandAction());

    ActionBuilder closeAction(this, Constants::OUTPUTPANE_CLOSE);
    closeAction
        .setIcon(Icons::CLOSE_SPLIT_BOTTOM.icon())
        .setText(Tr::tr("Close"))
        .addOnTriggered(this, &OutputPaneManager::slotHide)
        .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.ActionsGroup")
        .bindContextAction(&m_closeAction);
    closeButton->setDefaultAction(closeAction.commandAction());

    mpanes->addSeparator("Coreplugin.OutputPane.ActionsGroup");
}

void OutputPaneManager::initialize()
{
    setupButtons();

    const int currentIdx = m_instance->currentIndex();
    if (QTC_GUARD(currentIdx >= 0 && currentIdx < g_outputPanes.size()))
        m_instance->m_titleLabel->setText(g_outputPanes[currentIdx].pane->displayName());
    if (!useQuickButtonRow()) {
        m_instance->m_manageButton = new OutputPaneManageButton;
        m_instance->m_buttonsWidget->layout()->addWidget(m_instance->m_manageButton);
        connect(m_instance->m_manageButton,
                &OutputPaneManageButton::menuRequested,
                m_instance,
                &OutputPaneManager::popupMenu);
    }

    updateMaximizeButton(false); // give it an initial name

    m_instance->readSettings();

    connect(ModeManager::instance(), &ModeManager::currentModeChanged, m_instance, [] {
        const int index = m_instance->currentIndex();
        m_instance->updateActions(index >= 0 ? g_outputPanes.at(index).pane : nullptr);
    });

    m_instance->m_initialized = true;
}

void OutputPaneManager::updateButton(int row)
{
    QTC_ASSERT(row >= 0 && row < g_outputPanes.size(), return);
    const OutputPaneData &data = g_outputPanes.at(row);
    if (!data.button)
        return;
    data.button->setVisible(data.buttonVisible);
    data.button->setChecked(data.checked);
    data.button->setIconBadgeNumber(data.badge);
}

void OutputPaneManager::setupButtons()
{
    // A lazily loaded plugin with a pane of its own asks for all of this
    // again, so whatever the last time round drew has to go first. Cleared,
    // not just deleted: unregistering a pane's action below makes the model
    // say a row changed, and updateButton() has no way to tell a button that
    // is gone from one that is merely not built yet.
    for (auto &pane : g_outputPanes) {
        delete pane.button;
        pane.button = nullptr;
    }
    delete m_instance->m_quickButtonRow;
    m_instance->m_quickButtonRow = nullptr;

    QFontMetrics titleFm = m_instance->m_titleLabel->fontMetrics();
    int minTitleWidth = 0;

    Utils::sort(g_outputPanes, [](const OutputPaneData &d1, const OutputPaneData &d2) {
        return d1.pane->priorityInStatusBar() > d2.pane->priorityInStatusBar();
    });
    const int n = g_outputPanes.size();

    OutputPaneButtonModel * const model = m_instance->m_buttonModel;
    // Sorted and about to be renumbered, so nothing a row said still holds.
    model->reset();

    // Asked here and built after the loop, because what a row draws - its
    // number, its tooltip - is only settled once the loop has been through it.
    const bool quickRow = useQuickButtonRow();

    int shortcutNumber = 1;
    const Id baseId = "QtCreator.Pane.";
    for (int i = 0; i != n; ++i) {
        OutputPaneData &data = g_outputPanes[i];
        IOutputPane *outPane = data.pane;
        QWidget *widget = outPane->outputWidget(m_instance);
        // Not "is this widget in the stack": several panes reparent theirs to
        // the manager in outputWidget(), which takes it back out again, so on
        // a second pass they look like panes nobody has ever seen - and got a
        // second toolbar each, with the first left in the stack as an orphan.
        const int idx = i;
        if (!data.toolBar) {
            m_instance->m_outputWidgetPane->insertWidget(i, widget);

            connect(outPane, &IOutputPane::showPage, m_instance, [idx](int flags) {
                m_instance->showPage(idx, flags);
            });
            connect(outPane, &IOutputPane::hidePage, m_instance, &OutputPaneManager::slotHide);

            connect(outPane, &IOutputPane::togglePage, m_instance, [idx](int flags) {
                if (OutputPanePlaceHolder::isCurrentVisible() && m_instance->currentIndex() == idx)
                    m_instance->slotHide();
                else
                    m_instance->showPage(idx, flags);
            });

            connect(outPane, &IOutputPane::navigateStateUpdate, m_instance, [idx, outPane] {
                if (m_instance->currentIndex() == idx)
                    m_instance->updateActions(outPane);
            });

            // What a pane has to say about its own button, said to the model.
            connect(outPane, &IOutputPane::flashButton, model, [model, idx] {
                model->requestFlash(idx);
            });
            connect(outPane, &IOutputPane::setBadgeNumber, model, [model, idx](int number) {
                model->setBadge(idx, number);
            });

            auto *toolBar = new QToolBar(m_instance->m_opToolBarWidgets);
            toolBar->setContentsMargins(0, 0, 0, 0);
            // In the order the pane asked for, because a toolbar's order is
            // the pane's to decide and it interleaves kinds.
            for (const IOutputPane::ToolBarItem &item : outPane->toolBarItems()) {
                if (QWidget * const w = item.widget()) {
                    toolBar->addWidget(w);
                } else if (Utils::BaseAspect * const aspect = item.aspect()) {
                    auto * const toggle = new QToolButton;
                    if (!aspect->settingsKey().isEmpty())
                        toggle->setObjectName(QString::fromUtf8(aspect->settingsKey().view()));
                    toggle->setDefaultAction(aspect->action());
                    toolBar->addWidget(toggle);
                } else if (const Id commandId = item.command(); commandId.isValid()) {
                    QToolButton * const button
                        = Command::createToolButtonWithShortcutToolTip(commandId);
                    button->setObjectName(commandId.toString());
                    if (commandId == Constants::ZOOM_IN)
                        button->setIcon(Utils::Icons::PLUS_TOOLBAR.icon());
                    else if (commandId == Constants::ZOOM_OUT)
                        button->setIcon(Utils::Icons::MINUS_TOOLBAR.icon());
                    connect(button, &QToolButton::clicked, outPane, [outPane, commandId] {
                        if (commandId == Constants::ZOOM_IN)
                            emit outPane->zoomInRequested(1);
                        else if (commandId == Constants::ZOOM_OUT)
                            emit outPane->zoomOutRequested(1);
                    });
                    button->setEnabled(outPane->zoomEnabled());
                    connect(outPane, &IOutputPane::zoomEnabledChanged, button,
                            &QWidget::setEnabled);
                    toolBar->addWidget(button);
                }
            }
            auto stretch = new QWidget;
            stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
            toolBar->addWidget(stretch);

            data.toolBar = toolBar;
        }
        // Moved rather than added where they are already there, so that the
        // pane's row addresses the same pane in all three.
        m_instance->m_outputWidgetPane->insertWidget(i, widget);
        m_instance->m_opToolBarWidgets->insertWidget(i, data.toolBar);
        QTC_CHECK(m_instance->m_outputWidgetPane->indexOf(widget) == i);

        minTitleWidth = qMax(minTitleWidth, titleFm.horizontalAdvance(outPane->displayName()));

        data.id = baseId.withSuffix(outPane->id().toString());
        if (data.action) {
            ActionManager::unregisterAction(data.action, data.id);
            delete data.action;
            data.action = nullptr;
        }

        ActionBuilder paneAction(m_instance, data.id);
        paneAction
            .setText(outPane->displayName())
            .addOnTriggered(m_instance, [i] { m_instance->shortcutTriggered(i); })
            .setDefaultKeySequence(paneShortCut(shortcutNumber))
            .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.PanesGroup")
            .bindContextAction(&data.action);
        data.number = shortcutNumber;
        // The command outlives a second pass, unlike the context action, so
        // the listener below has to go before it is made again.
        if (data.commandAction)
            disconnect(data.commandAction, &QAction::changed, model, nullptr);
        data.commandAction = paneAction.commandAction();
        // What the tooltip says is which keys reach the pane, so it follows a
        // reader rebinding them - the button the widget row draws does the
        // same, by listening to the very same action.
        connect(data.commandAction, &QAction::changed, model, [model, i] {
            model->toolTipChanged(i);
        });
        if (!quickRow) {
            auto button = new OutputPaneToggleButton(shortcutNumber,
                                                     outPane->displayName(),
                                                     paneAction.commandAction());
            data.button = button;
            connect(button, &OutputPaneToggleButton::contextMenuRequested, m_instance, [] {
                m_instance->popupMenu();
            });

            // At the pane's own position, not appended: on a second pass the
            // manage button is already in the layout and has to stay last.
            auto * const row = qobject_cast<QBoxLayout *>(m_instance->m_buttonsWidget->layout());
            if (QTC_GUARD(row))
                row->insertWidget(i, button);
            connect(button, &QAbstractButton::clicked, m_instance, [i] {
                m_instance->buttonTriggered(i);
            });
        }

        ++shortcutNumber;

        // A pane with no priority in the status bar has no button until a
        // reader asks for one from the menu.
        model->setButtonVisible(i, outPane->priorityInStatusBar() >= 0);
        m_instance->updateButton(i);
    }

    if (quickRow) {
        m_instance->m_quickButtonRow = createButtonRow();
        // At the front, not appended: the manage button is put in the layout
        // once and stays there, so a later run would land behind it.
        auto * const layout = qobject_cast<QBoxLayout *>(m_instance->m_buttonsWidget->layout());
        if (QTC_GUARD(layout) && m_instance->m_quickButtonRow)
            layout->insertWidget(0, m_instance->m_quickButtonRow);
    }

    m_instance->m_titleLabel->setMinimumWidth(
        minTitleWidth + m_instance->m_titleLabel->contentsMargins().left()
        + m_instance->m_titleLabel->contentsMargins().right());
}

OutputPaneManager::~OutputPaneManager() = default;

void OutputPaneManager::shortcutTriggered(int idx)
{
    IOutputPane *outputPane = g_outputPanes.at(idx).pane;
    // Now check the special case, the output window is already visible,
    // we are already on that page but the outputpane doesn't have focus
    // then just give it focus.
    int current = currentIndex();
    if (OutputPanePlaceHolder::isCurrentVisible() && current == idx) {
        if ((!m_outputWidgetPane->isActiveWindow() || !outputPane->hasFocus())
            && outputPane->canFocus()) {
            outputPane->setFocus();
            ICore::raiseWindow(m_outputWidgetPane);
        } else {
            slotHide();
        }
    } else {
        // Else do the same as clicking on the button does.
        buttonTriggered(idx);
    }
}

int OutputPaneManager::outputPaneHeightSetting()
{
    return m_instance->m_outputPaneHeightSetting;
}

void OutputPaneManager::setOutputPaneHeightSetting(int value)
{
    m_instance->m_outputPaneHeightSetting = value;
}

bool OutputPaneManager::initialized()
{
    return m_instance && m_instance->m_initialized;
}

void OutputPaneManager::toggleMaximized()
{
    OutputPanePlaceHolder *ph = OutputPanePlaceHolder::getCurrent();
    QTC_ASSERT(ph, return);

    if (!ph->isVisible()) // easier than disabling/enabling the action
        return;
    ph->setMaximized(!ph->isMaximized());
}

void OutputPaneManager::buttonTriggered(int idx)
{
    QTC_ASSERT(idx >= 0, return);
    if (idx == currentIndex() && OutputPanePlaceHolder::isCurrentVisible()) {
        // we should toggle and the page is already visible and we are actually closeable
        slotHide();
    } else {
        showPage(idx, IOutputPane::ModeSwitch | IOutputPane::WithFocus);
    }
}

void OutputPaneManager::readSettings()
{
    QtcSettings *settings = ICore::settings();
    int num = settings->beginReadArray(outputPaneSettingsKeyC);
    for (int i = 0; i < num; ++i) {
        settings->setArrayIndex(i);
        Id id = Id::fromSetting(settings->value(outputPaneIdKeyC));
        const int idx = Utils::indexOf(g_outputPanes, Utils::equal(&OutputPaneData::id, id));
        if (idx < 0) // happens for e.g. disabled plugins (with outputpanes) that were loaded before
            continue;
        m_buttonModel->setButtonVisible(idx, settings->value(outputPaneVisibleKeyC).toBool());
    }
    settings->endArray();

    m_outputPaneHeightSetting
        = settings->value("OutputPanePlaceHolder/Height", 0).toInt();
    const int currentIdx
        = settings->value("OutputPanePlaceHolder/CurrentIndex", 0).toInt();
    if (QTC_GUARD(currentIdx >= 0 && currentIdx < g_outputPanes.size()))
        setCurrentIndex(currentIdx);
}

void OutputPaneManager::updateActions(IOutputPane *pane)
{
    const bool enabledForMode = m_buttonsWidget->isVisibleTo(m_buttonsWidget->window())
                                || OutputPanePlaceHolder::modeHasOutputPanePlaceholder(
                                    ModeManager::currentModeId());
    m_clearAction->setEnabled(enabledForMode);
    m_minMaxAction->setEnabled(enabledForMode);
    m_prevAction->setEnabled(enabledForMode && pane && pane->canNavigate() && pane->canPrevious());
    m_nextAction->setEnabled(enabledForMode && pane && pane->canNavigate() && pane->canNext());
    for (const OutputPaneData &d : std::as_const(g_outputPanes))
        d.action->setEnabled(enabledForMode);
}

void OutputPaneManager::slotNext()
{
    int idx = currentIndex();
    ensurePageVisible(idx);
    IOutputPane *out = g_outputPanes.at(idx).pane;
    if (out->canNext())
        out->goToNext();
}

void OutputPaneManager::slotPrev()
{
    int idx = currentIndex();
    ensurePageVisible(idx);
    IOutputPane *out = g_outputPanes.at(idx).pane;
    if (out->canPrevious())
        out->goToPrev();
}

void OutputPaneManager::slotHide()
{
    OutputPanePlaceHolder *ph = OutputPanePlaceHolder::getCurrent();
    if (ph) {
        emit ph->visibilityChangeRequested(false);
        ph->setVisible(false);
        int idx = currentIndex();
        QTC_ASSERT(idx >= 0, return);
        m_buttonModel->setChecked(idx, false);
        g_outputPanes.at(idx).pane->visibilityChanged(false);
        if (IEditor *editor = EditorManager::currentEditor()) {
            QWidget *w = editor->widget()->focusWidget();
            if (!w)
                w = editor->widget();
            w->setFocus();
        }
    }
}

void OutputPaneManager::ensurePageVisible(int idx)
{
    if (currentIndex() != idx)
        setCurrentIndex(idx);
}

void OutputPaneManager::showPage(int idx, int flags)
{
    QTC_ASSERT(idx >= 0, return);
    OutputPanePlaceHolder *ph = OutputPanePlaceHolder::getCurrent();

    if (!ph && flags & IOutputPane::ModeSwitch) {
        // In this mode we don't have a placeholder
        // switch to the output mode and switch the page
        ModeManager::activateMode(Id(Constants::MODE_EDIT));
        ph = OutputPanePlaceHolder::getCurrent();
    }

    if (!ph) {
        // Nowhere to show it in this mode, so the button is all there is to
        // say something arrived.
        m_buttonModel->requestFlash(idx);
        return;
    }

    emit ph->visibilityChangeRequested(true);
    ph->setVisible(true);

    ensurePageVisible(idx);
    IOutputPane *out = g_outputPanes.at(idx).pane;
    if (flags & IOutputPane::WithFocus) {
        if (out->canFocus())
            out->setFocus();
        ICore::raiseWindow(m_outputWidgetPane);
    }

    if (flags & IOutputPane::EnsureSizeHint)
        ph->ensureSizeHintAsMinimum();
}

void OutputPaneManager::focusInEvent(QFocusEvent *e)
{
    if (QWidget *w = m_outputWidgetPane->currentWidget())
        w->setFocus(e->reason());
}

bool OutputPaneManager::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_buttonsWidget && (e->type() == QEvent::Show || e->type() == QEvent::Hide)) {
        const int index = currentIndex();
        updateActions(index >= 0 ? g_outputPanes.at(index).pane : nullptr);
    }
    return false;
}

void OutputPaneManager::setCurrentIndex(int idx)
{
    static int lastIndex = -1;

    if (lastIndex != -1) {
        m_buttonModel->setChecked(lastIndex, false);
        g_outputPanes.at(lastIndex).pane->visibilityChanged(false);
    }

    if (idx != -1) {
        m_outputWidgetPane->setCurrentIndex(idx);
        m_opToolBarWidgets->setCurrentIndex(idx);

        IOutputPane * const pane = g_outputPanes.at(idx).pane;
        // A pane made current has a button, whatever the menu last said: this
        // is where a reader gets one back after hiding it.
        m_buttonModel->setButtonVisible(idx, true);
        if (OutputPanePlaceHolder::isCurrentVisible())
            pane->visibilityChanged(true);

        updateActions(pane);
        m_buttonModel->setChecked(idx, OutputPanePlaceHolder::isCurrentVisible());
        m_titleLabel->setText(pane->displayName());
    }

    lastIndex = idx;
}

void OutputPaneManager::fillManageMenu(QMenu *menu)
{
    int idx = 0;
    for (const OutputPaneData &data : std::as_const(g_outputPanes)) {
        QAction *act = menu->addAction(data.pane->displayName());
        act->setCheckable(true);
        act->setChecked(m_buttonModel->isButtonVisible(idx));
        connect(act, &QAction::triggered, this, [this, pane = data.pane, idx] {
            if (m_buttonModel->isButtonVisible(idx)) {
                pane->visibilityChanged(false);
                m_buttonModel->setChecked(idx, false);
                m_buttonModel->setButtonVisible(idx, false);
            } else {
                // The entry is about the button, and showPage() gives up
                // without one where there is nowhere to put the pane.
                m_buttonModel->setButtonVisible(idx, true);
                showPage(idx, IOutputPane::ModeSwitch);
            }
        });
        ++idx;
    }

    menu->addSeparator();
    QAction *reset = menu->addAction(Tr::tr("Reset to Default"));
    connect(reset, &QAction::triggered, this, [this] {
        for (int i = 0; i < g_outputPanes.size(); ++i) {
            const OutputPaneData &data = g_outputPanes.at(i);
            const bool buttonVisible = data.pane->priorityInStatusBar() >= 0;
            const bool paneVisible = buttonVisible && currentIndex() == i
                                     && OutputPanePlaceHolder::isCurrentVisible();
            m_buttonModel->setChecked(i, paneVisible);
            m_buttonModel->setButtonVisible(i, buttonVisible);
        }
    });
}

void OutputPaneManager::popupMenu()
{
    QMenu menu;
    fillManageMenu(&menu);
    menu.exec(QCursor::pos());
}

void OutputPaneManager::saveSettings() const
{
    QtcSettings *settings = ICore::settings();
    const int n = g_outputPanes.size();
    settings->beginWriteArray(outputPaneSettingsKeyC, n);
    for (int i = 0; i < n; ++i) {
        const OutputPaneData &data = g_outputPanes.at(i);
        settings->setArrayIndex(i);
        settings->setValue(outputPaneIdKeyC, data.id.toSetting());
        settings->setValue(outputPaneVisibleKeyC, data.buttonVisible);
    }
    settings->endArray();
    int heightSetting = m_outputPaneHeightSetting;
    // update if possible
    if (OutputPanePlaceHolder *curr = OutputPanePlaceHolder::getCurrent())
        heightSetting = curr->nonMaximizedSize();
    settings->setValue("OutputPanePlaceHolder/Height", heightSetting);
    settings->setValue("OutputPanePlaceHolder/CurrentIndex", currentIndex());
}

void OutputPaneManager::clearPage()
{
    int idx = currentIndex();
    if (idx >= 0)
        g_outputPanes.at(idx).pane->clearContents();
}

int OutputPaneManager::currentIndex() const
{
    return m_outputWidgetPane->currentIndex();
}


///////////////////////////////////////////////////////////////////////
//
// OutputPaneToolButton
//
///////////////////////////////////////////////////////////////////////

OutputPaneToggleButton::OutputPaneToggleButton(int number, const QString &text, QAction *action)
    : m_number(QString::number(number))
    , m_text(text)
    , m_action(action)
    , m_flashTimer(new QTimeLine(1000, this))
{
    setFocusPolicy(Qt::NoFocus);
    setCheckable(true);
    QFont fnt = QApplication::font();
    setFont(fnt);
    if (m_action)
        connect(m_action, &QAction::changed, this, &OutputPaneToggleButton::updateToolTip);

    m_flashTimer->setDirection(QTimeLine::Forward);
    m_flashTimer->setEasingCurve(QEasingCurve::SineCurve);
    m_flashTimer->setFrameRange(0, 92);
    auto updateSlot = QOverload<>::of(&QWidget::update);
    connect(m_flashTimer, &QTimeLine::valueChanged, this, updateSlot);
    connect(m_flashTimer, &QTimeLine::finished, this, updateSlot);
    updateToolTip();
}

void OutputPaneToggleButton::updateToolTip()
{
    QTC_ASSERT(m_action, return);
    setToolTip(m_action->toolTip());
}

QSize OutputPaneToggleButton::sizeHint() const
{
    ensurePolished();

    QSize s = fontMetrics().size(Qt::TextSingleLine, m_text);

    // Expand to account for border image
    s.rwidth() += numberAreaWidth() + 1 + buttonBorderWidth + buttonBorderWidth;

    if (!m_badgeNumberLabel.text().isNull())
        s.rwidth() += m_badgeNumberLabel.sizeHint().width() + 1;

    return s;
}

static QRect bgRect(const QRect &widgetRect)
{
    // Removes/compensates the left and right margins of StyleHelper::drawPanelBgRect
    return StyleHelper::toolbarStyle() == StyleHelper::ToolbarStyle::Compact
               ? widgetRect : widgetRect.adjusted(-2, 0, 2, 0);
}

void OutputPaneToggleButton::paintEvent(QPaintEvent*)
{
    const QFontMetrics fm = fontMetrics();
    const int baseLine = (height() - fm.height() + 1) / 2 + fm.ascent();
    const int numberWidth = fm.horizontalAdvance(m_number);

    QPainter p(this);

    QStyleOption styleOption;
    styleOption.initFrom(this);
    const bool hovered = !HostOsInfo::isMacHost() && (styleOption.state & QStyle::State_MouseOver);

    if (creatorTheme()->flag(Theme::FlatToolBars)) {
        Theme::Color c = Theme::BackgroundColorDark;

        if (hovered)
            c = Theme::BackgroundColorHover;
        else if (isDown() || isChecked())
            c = Theme::BackgroundColorSelected;

        if (c != Theme::BackgroundColorDark)
            StyleHelper::drawPanelBgRect(&p, bgRect(rect()), creatorColor(c));
    } else {
        const QImage *image = nullptr;
        if (isDown()) {
            static const QImage pressed(
                        StyleHelper::dpiSpecificImageFile(":/utils/images/panel_button_pressed.png"));
            image = &pressed;
        } else if (isChecked()) {
            if (hovered) {
                static const QImage checkedHover(
                            StyleHelper::dpiSpecificImageFile(":/utils/images/panel_button_checked_hover.png"));
                image = &checkedHover;
            } else {
                static const QImage checked(
                            StyleHelper::dpiSpecificImageFile(":/utils/images/panel_button_checked.png"));
                image = &checked;
            }
        } else {
            if (hovered) {
                static const QImage hover(
                            StyleHelper::dpiSpecificImageFile(":/utils/images/panel_button_hover.png"));
                image = &hover;
            } else {
                static const QImage button(
                            StyleHelper::dpiSpecificImageFile(":/utils/images/panel_button.png"));
                image = &button;
            }
        }
        if (image)
            StyleHelper::drawCornerImage(*image, &p, rect(), numberAreaWidth(), buttonBorderWidth, buttonBorderWidth, buttonBorderWidth);
    }

    if (m_flashTimer->state() == QTimeLine::Running)
    {
        QColor c = creatorColor(Theme::OutputPaneButtonFlashColor);
        c.setAlpha (m_flashTimer->currentFrame());
        if (creatorTheme()->flag(Theme::FlatToolBars))
            StyleHelper::drawPanelBgRect(&p, bgRect(rect()), c);
        else
            p.fillRect(rect().adjusted(numberAreaWidth(), 1, -1, -1), c);
    }

    p.setFont(font());
    p.setPen(creatorColor(Theme::OutputPaneToggleButtonTextColorChecked));
    p.drawText((numberAreaWidth() - numberWidth) / 2, baseLine, m_number);
    if (!isChecked())
        p.setPen(creatorColor(Theme::OutputPaneToggleButtonTextColorUnchecked));
    int leftPart = numberAreaWidth() + buttonBorderWidth;
    int labelWidth = 0;
    if (!m_badgeNumberLabel.text().isEmpty()) {
        const QSize labelSize = m_badgeNumberLabel.sizeHint();
        labelWidth = labelSize.width() + 3;
        m_badgeNumberLabel.paint(&p, width() - labelWidth, (height() - labelSize.height()) / 2, isChecked());
    }
    p.drawText(leftPart, baseLine, fm.elidedText(m_text, Qt::ElideRight, width() - leftPart - 1 - labelWidth));
}

void OutputPaneToggleButton::checkStateSet()
{
    //Stop flashing when button is checked
    QToolButton::checkStateSet();
    m_flashTimer->stop();
}

void OutputPaneToggleButton::flash(int count)
{
    setVisible(true);
    //Start flashing if button is not checked
    if (!isChecked()) {
        m_flashTimer->setLoopCount(count);
        if (m_flashTimer->state() != QTimeLine::Running)
            m_flashTimer->start();
        update();
    }
}

void OutputPaneToggleButton::setIconBadgeNumber(int number)
{
    QString text = (number ? QString::number(number) : QString());
    m_badgeNumberLabel.setText(text);
    updateGeometry();
}

void OutputPaneToggleButton::contextMenuEvent(QContextMenuEvent *)
{
    emit contextMenuRequested();
}

///////////////////////////////////////////////////////////////////////
//
// OutputPaneManageButton
//
///////////////////////////////////////////////////////////////////////

OutputPaneManageButton::OutputPaneManageButton()
{
    setFocusPolicy(Qt::NoFocus);
    setCheckable(true);
    setFixedWidth(StyleHelper::toolbarStyle() == Utils::StyleHelper::ToolbarStyle::Compact ? 17
                                                                                           : 21);
    connect(this, &QToolButton::clicked, this, &OutputPaneManageButton::menuRequested);
}

void OutputPaneManageButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (!creatorTheme()->flag(Theme::FlatToolBars)) {
        static const QImage button(StyleHelper::dpiSpecificImageFile(QStringLiteral(":/utils/images/panel_manage_button.png")));
        StyleHelper::drawCornerImage(button, &p, rect(), buttonBorderWidth, buttonBorderWidth, buttonBorderWidth, buttonBorderWidth);
    }
    QStyle *s = style();
    QStyleOption arrowOpt;
    arrowOpt.initFrom(this);
    constexpr int arrowSize = 8;
    arrowOpt.rect = QRect(0, 0, arrowSize, arrowSize);
    arrowOpt.rect.moveCenter(rect().center());
    arrowOpt.rect.translate(0, -3);
    s->drawPrimitive(QStyle::PE_IndicatorArrowUp, &arrowOpt, &p, this);
    arrowOpt.rect.translate(0, 6);
    s->drawPrimitive(QStyle::PE_IndicatorArrowDown, &arrowOpt, &p, this);
}

void OutputPaneManageButton::contextMenuEvent(QContextMenuEvent *)
{
    emit menuRequested();
}

BadgeLabel::BadgeLabel()
{
    m_font = QApplication::font();
    m_font.setBold(true);
    m_font.setPixelSize(11);
}

void BadgeLabel::paint(QPainter *p, int x, int y, bool isChecked)
{
    const QRectF rect(QRect(QPoint(x, y), m_size));
    p->save();

    p->setBrush(creatorColor(isChecked? Theme::BadgeLabelBackgroundColorChecked
                                      : Theme::BadgeLabelBackgroundColorUnchecked));
    p->setPen(Qt::NoPen);
    p->setRenderHint(QPainter::Antialiasing, true);
    p->drawRoundedRect(rect, m_padding, m_padding, Qt::AbsoluteSize);

    p->setFont(m_font);
    p->setPen(creatorColor(isChecked ? Theme::BadgeLabelTextColorChecked
                                     : Theme::BadgeLabelTextColorUnchecked));
    p->drawText(rect, Qt::AlignCenter, m_text);

    p->restore();
}

void BadgeLabel::setText(const QString &text)
{
    m_text = text;
    calculateSize();
}

QString BadgeLabel::text() const
{
    return m_text;
}

QSize BadgeLabel::sizeHint() const
{
    return m_size;
}

void BadgeLabel::calculateSize()
{
    const QFontMetrics fm(m_font);
    m_size = fm.size(Qt::TextSingleLine, m_text);
    m_size.setWidth(m_size.width() + m_padding * 1.5);
    m_size.setHeight(2 * m_padding + 1); // Needs to be uneven for pixel perfect vertical centering in the button
}

#ifdef WITH_TESTS

// The row of pane buttons in the status bar. What is on trial here is that the
// row's state is the model's: the buttons draw it, the session saves it, and a
// pane reporting a badge or asking to be noticed reaches the model rather than
// a widget.
class OutputPaneButtonModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheButtonRowIsAModelOfThePanes()
    {
        OutputPaneButtonModel * const model = OutputPaneManager::instance()->m_buttonModel;
        QVERIFY(model);

        const QList<IOutputPane *> panes = IOutputPane::allOutputPanes();
        QVERIFY2(!panes.isEmpty(), "no output panes are registered, so this tests nothing");
        QCOMPARE(model->rowCount(), panes.size());

        int previousPriority = std::numeric_limits<int>::max();
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row);
            // The name a reader sees on the button, and the Alt+<n> that
            // reaches it: the numbers run 1..N down the row.
            QCOMPARE(index.data(Qt::DisplayRole).toString(), panes.at(row)->displayName());
            QCOMPARE(index.data(OutputPaneButtonModel::NumberRole).toInt(), row + 1);

            // The order is the status bar's: by priority, highest first.
            const int priority = panes.at(row)->priorityInStatusBar();
            QVERIFY2(priority <= previousPriority,
                     qPrintable(QString("row %1 (%2) has priority %3, after %4")
                                    .arg(row).arg(panes.at(row)->displayName())
                                    .arg(priority).arg(previousPriority)));
            previousPriority = priority;
        }
    }

    void testWhatAPaneSaysAboutItsButtonReachesTheModel()
    {
        OutputPaneButtonModel * const model = OutputPaneManager::instance()->m_buttonModel;
        QVERIFY(model);
        QVERIFY(model->rowCount() > 0);
        const int row = 0;
        IOutputPane * const pane = IOutputPane::allOutputPanes().at(row);
        const QModelIndex index = model->index(row);

        // Left as it was found: this is the running shell's own row.
        const QVariant badge = index.data(OutputPaneButtonModel::BadgeRole);
        const QScopeGuard restoreBadge([pane, badge] {
            pane->setIconBadgeNumber(badge.toString().toInt()); });

        // Said the way a pane says it, which is what the connection under
        // test is for - not by calling the model's own setter.
        pane->setIconBadgeNumber(7);
        QCOMPARE(index.data(OutputPaneButtonModel::BadgeRole).toString(), QString("7"));
        pane->setIconBadgeNumber(0);
        QVERIFY2(index.data(OutputPaneButtonModel::BadgeRole).toString().isEmpty(),
                 "a pane with nothing to report still has a badge");

        // And asking to be noticed is an event: it leaves no state behind.
        QSignalSpy flashes(model, &OutputPaneButtonModel::flashRequested);
        pane->flash();
        QCOMPARE(flashes.size(), 1);
        QCOMPARE(flashes.first().first().toInt(), row);
    }

    void testAPaneMadeCurrentGetsItsButtonBack()
    {
        // A reader who hid a pane's button from the manage menu and then
        // reaches the pane another way - a shortcut, a build failing - gets
        // the button back. That has to reach the model, because the model is
        // what the session saves: a button on screen and a "hidden" in the
        // settings is how it comes back missing tomorrow.
        OutputPaneManager * const manager = OutputPaneManager::instance();
        OutputPaneButtonModel * const model = manager->m_buttonModel;
        QVERIFY(model);
        QVERIFY(model->rowCount() > 1);
        const int row = 1;

        const int wasCurrent = manager->currentIndex();
        const bool wasVisible = model->isButtonVisible(row);
        const QScopeGuard restore([manager, model, wasCurrent, wasVisible] {
            if (wasCurrent >= 0)
                manager->setCurrentIndex(wasCurrent);
            model->setButtonVisible(row, wasVisible);
        });

        model->setButtonVisible(row, false);
        QVERIFY(!model->isButtonVisible(row));
        manager->setCurrentIndex(row);
        QVERIFY2(model->isButtonVisible(row),
                 "the pane is the current one and the model still says it has no button");
    }

    void testTheManageMenuListsEveryPaneAndSaysWhichHaveButtons()
    {
        OutputPaneManager * const manager = OutputPaneManager::instance();
        OutputPaneButtonModel * const model = manager->m_buttonModel;
        QVERIFY(model);
        const QList<IOutputPane *> panes = IOutputPane::allOutputPanes();
        QVERIFY(!panes.isEmpty());

        QMenu menu;
        manager->fillManageMenu(&menu);

        const QList<QAction *> actions = menu.actions();
        QCOMPARE(actions.size(), panes.size() + 2);
        for (int row = 0; row < panes.size(); ++row) {
            QAction * const act = actions.at(row);
            QCOMPARE(act->text(), panes.at(row)->displayName());
            QVERIFY2(act->isCheckable(), "an entry that cannot be ticked says nothing");
            QCOMPARE(act->isChecked(), model->isButtonVisible(row));
        }
        QVERIFY2(actions.at(panes.size())->isSeparator(),
                 "the panes run straight into Reset to Default");
        QCOMPARE(actions.last()->text(), Tr::tr("Reset to Default"));
    }

    void testTheManageMenuIsTheWayBackForAHiddenButton()
    {
        // Taking a button away and putting it back is the only thing this
        // menu is for, and it is the only route back: a pane with no button
        // cannot be reached by pressing one.
        OutputPaneManager * const manager = OutputPaneManager::instance();
        OutputPaneButtonModel * const model = manager->m_buttonModel;
        QVERIFY(model);
        QVERIFY(model->rowCount() > 0);
        const int row = 0;
        const bool wasVisible = model->isButtonVisible(row);
        const QScopeGuard restore([model, wasVisible] {
            model->setButtonVisible(row, wasVisible); });

        model->setButtonVisible(row, true);
        QMenu hide;
        manager->fillManageMenu(&hide);
        QVERIFY(hide.actions().at(row)->isChecked());
        hide.actions().at(row)->trigger();
        QVERIFY2(!model->isButtonVisible(row), "the menu said yes and the button stayed");

        // Filled again, because what the entries say is read when the menu is
        // built: a stale tick is a reader told the opposite of what is there.
        QMenu show;
        manager->fillManageMenu(&show);
        QVERIFY2(!show.actions().at(row)->isChecked(),
                 "the menu still ticks a pane whose button it just took away");
        show.actions().at(row)->trigger();
        QVERIFY2(model->isButtonVisible(row), "there is no way back to a hidden button");
    }

    void testResetToDefaultGivesBackTheButtonsThePanesAskedFor()
    {
        OutputPaneManager * const manager = OutputPaneManager::instance();
        OutputPaneButtonModel * const model = manager->m_buttonModel;
        QVERIFY(model);
        const QList<IOutputPane *> panes = IOutputPane::allOutputPanes();
        QVERIFY(!panes.isEmpty());

        QList<bool> wasVisible;
        for (int row = 0; row < panes.size(); ++row)
            wasVisible << model->isButtonVisible(row);
        const QScopeGuard restore([model, wasVisible] {
            for (int row = 0; row < wasVisible.size(); ++row)
                model->setButtonVisible(row, wasVisible.at(row)); });

        for (int row = 0; row < panes.size(); ++row)
            model->setButtonVisible(row, false);

        QMenu menu;
        manager->fillManageMenu(&menu);
        menu.actions().last()->trigger();

        for (int row = 0; row < panes.size(); ++row) {
            const bool wanted = panes.at(row)->priorityInStatusBar() >= 0;
            QVERIFY2(model->isButtonVisible(row) == wanted,
                     qPrintable(QString("%1 has a button: %2, and asked for one: %3")
                                    .arg(panes.at(row)->displayName())
                                    .arg(model->isButtonVisible(row)).arg(wanted)));
        }
    }

    void testTheTooltipNamesTheKeysThatReachThePane()
    {
        // A reader hovers a button to find out which keys open the pane, so
        // the tooltip is the command's, not the plain context action's - the
        // latter is never given the shortcut at all. And it follows a rebind,
        // because the Keyboard settings page can change it while the row is
        // on screen.
        OutputPaneButtonModel * const model = OutputPaneManager::instance()->m_buttonModel;
        QVERIFY(model);
        QVERIFY(model->rowCount() > 0);
        const int row = 0;
        const QModelIndex index = model->index(row);

        Command * const command = ActionManager::command(g_outputPanes.at(row).id);
        QVERIFY2(command, "the pane's shortcut is registered nowhere");
        const QList<QKeySequence> wasBound = command->keySequences();
        const QScopeGuard rebind([command, wasBound] { command->setKeySequences(wasBound); });

        command->setKeySequences({QKeySequence("Ctrl+Alt+Shift+F9")});
        QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
        const QString withShortcut = index.data(Qt::ToolTipRole).toString();
        QVERIFY2(withShortcut.contains("F9"),
                 qPrintable("the tooltip says \"" + withShortcut + "\" and not which keys"));

        // Rebound while the row is drawn: the row has to be told, or it goes
        // on offering keys that no longer reach anything.
        command->setKeySequences({QKeySequence("Ctrl+Alt+Shift+F8")});
        QVERIFY2(!changes.isEmpty(), "the shortcut changed and the row was never told");
        bool sawToolTip = false;
        for (const QList<QVariant> &change : changes) {
            const QList<int> roles = change.at(2).value<QList<int>>();
            sawToolTip = sawToolTip || roles.contains(int(Qt::ToolTipRole));
        }
        QVERIFY2(sawToolTip, "the row was told about something other than the tooltip");
        QVERIFY(index.data(Qt::ToolTipRole).toString().contains("F8"));
    }

    void testASecondPassRebuildsTheRowRatherThanDoublingIt()
    {
        // A lazily loaded plugin with a pane of its own makes the shell build
        // the whole row again, on a status bar that already holds one. What
        // is on trial is that the second pass leaves the same widgets there
        // as the first, in the same order - a row appended to a row is two
        // rows, and the manage button is only ever put in place once.
        auto * const layout
            = qobject_cast<QBoxLayout *>(OutputPaneManager::instance()->m_buttonsWidget->layout());
        QVERIFY(layout);
        const int before = layout->count();
        QVERIFY2(before > 0, "the status bar's row is empty, so nothing here is being tested");

        OutputPaneManager::setupButtons();

        QCOMPARE(layout->count(), before);

        // Whichever row is built, the arrow that manages it comes last: the
        // Quick row draws its own, the widget row has a QToolButton beside it.
        OutputPaneManager * const manager = OutputPaneManager::instance();
        QWidget * const last = layout->itemAt(layout->count() - 1)->widget();
        if (manager->m_manageButton)
            QCOMPARE(last, static_cast<QWidget *>(manager->m_manageButton));
        else
            QCOMPARE(last, manager->m_quickButtonRow);
    }

    void testThePaneNamesItsZoomButtonsRatherThanBuildingThem()
    {
        // Every pane used to construct its own pair of zoom QToolButtons -
        // thirteen pairs of the same two - and reach into them to enable
        // them. A toolbar that is not a QToolBar cannot host a widget, so the
        // pane names the commands and says whether they apply.
        const QList<IOutputPane *> panes = IOutputPane::allOutputPanes();
        QVERIFY(!panes.isEmpty());
        IOutputPane * const pane = panes.first();

        QVERIFY2(pane->toolBarCommands().contains(Utils::Id(Constants::ZOOM_IN)),
                 "a pane offers no way to zoom in");
        QVERIFY2(pane->toolBarCommands().contains(Utils::Id(Constants::ZOOM_OUT)),
                 "a pane offers no way to zoom out");

        // And the toolbar built one from that name.
        QWidget * const toolBar = m_instance->m_opToolBarWidgets->widget(0);
        QVERIFY2(toolBar, "the first pane has no toolbar");
        auto * const zoomIn = toolBar->findChild<QToolButton *>(Constants::ZOOM_IN);
        QVERIFY2(zoomIn, "nothing in the toolbar answers to the zoom-in command");

        const bool wasEnabled = pane->zoomEnabled();
        const QScopeGuard restore([pane, wasEnabled] {
            pane->setZoomButtonsEnabled(wasEnabled); });

        pane->setZoomButtonsEnabled(true);
        QVERIFY(zoomIn->isEnabled());
        pane->setZoomButtonsEnabled(false);
        QVERIFY2(!zoomIn->isEnabled(),
                 "the pane said zooming does not apply and the button stayed on");

        // Said once: the pane's answer is state, so repeating it is silent.
        QSignalSpy changes(pane, &IOutputPane::zoomEnabledChanged);
        pane->setZoomButtonsEnabled(false);
        QVERIFY2(changes.isEmpty(), "a pane that changed nothing told the toolbar anyway");
    }

    void testASecondPassLeavesOnePaneOneToolbar()
    {
        // setupButtons() asked "is this widget in the stack" to decide whether
        // it had seen a pane before - and several panes reparent their widget
        // to the manager in outputWidget(), which takes it out of the stack.
        // So on every pass they looked new and got another toolbar, with the
        // last one orphaned. The stack grew 12 to 16 in a single pass.
        const int panes = IOutputPane::allOutputPanes().size();
        QVERIFY(panes > 0);
        const auto counts = [] {
            return QString("%1 panes, %2 toolbars, %3 contents")
                .arg(IOutputPane::allOutputPanes().size())
                .arg(m_instance->m_opToolBarWidgets->count())
                .arg(m_instance->m_outputWidgetPane->count());
        };
        QVERIFY2(m_instance->m_opToolBarWidgets->count() == panes
                     && m_instance->m_outputWidgetPane->count() == panes,
                 qPrintable("before: " + counts()));

        QWidgetList contentsBefore;
        QWidgetList toolBarsBefore;
        for (int row = 0; row < panes; ++row) {
            contentsBefore << m_instance->m_outputWidgetPane->widget(row);
            toolBarsBefore << m_instance->m_opToolBarWidgets->widget(row);
        }

        OutputPaneManager::setupButtons();

        QVERIFY2(m_instance->m_opToolBarWidgets->count() == panes
                     && m_instance->m_outputWidgetPane->count() == panes,
                 qPrintable("after: " + counts()));

        for (int row = 0; row < panes; ++row) {
            QCOMPARE(m_instance->m_outputWidgetPane->widget(row), contentsBefore.at(row));
            QCOMPARE(m_instance->m_opToolBarWidgets->widget(row), toolBarsBefore.at(row));
        }

        // Each row holds what it held, rather than having shuffled. Recorded
        // rather than asked for: outputWidget() reparents, which is the side
        // effect this whole test is about, so calling it here would be the
        // test causing what it is looking for.
    }

    void testAPaneDecidesTheOrderOfItsOwnToolbar()
    {
        // Naming the toggles moved them: a pane's widgets, its aspects and
        // its commands were appended in that fixed order, so Console's three
        // toggles ended up after the gap and the status label they used to
        // come before. Nothing could see it, because nothing asked what order
        // a toolbar is in.
        const QList<IOutputPane *> panes = IOutputPane::allOutputPanes();
        IOutputPane *interleaved = nullptr;
        for (IOutputPane * const pane : panes) {
            const QList<IOutputPane::ToolBarItem> items = pane->toolBarItems();
            bool seenAspect = false;
            for (const IOutputPane::ToolBarItem &item : items) {
                if (item.aspect())
                    seenAspect = true;
                else if (item.widget() && seenAspect && !interleaved)
                    interleaved = pane;
            }
        }
        QVERIFY2(interleaved,
                 "no pane puts a widget after a toggle, so ordering is untested");

        // And the toolbar drew them in that order rather than in kind order.
        const int row = panes.indexOf(interleaved);
        QWidget * const toolBar = m_instance->m_opToolBarWidgets->widget(row);
        QVERIFY(toolBar);
        const QList<QWidget *> drawn = toolBar->findChildren<QWidget *>(
            QString(), Qt::FindDirectChildrenOnly);

        QStringList wanted;
        for (const IOutputPane::ToolBarItem &item : interleaved->toolBarItems()) {
            if (item.aspect() && !item.aspect()->settingsKey().isEmpty())
                wanted << QString::fromUtf8(item.aspect()->settingsKey().view());
            else if (item.command().isValid())
                wanted << item.command().toString();
        }
        QVERIFY2(wanted.size() >= 2, "too few named items to say anything about order");

        QStringList got;
        for (QWidget * const w : drawn) {
            if (!w->objectName().isEmpty() && wanted.contains(w->objectName()))
                got << w->objectName();
        }
        QCOMPARE(got, wanted);
    }

    void testAPaneNamesItsTogglesAsAspectsRatherThanButtons()
    {
        // A toggle in an output pane's toolbar is a setting: it has an icon, a
        // tooltip, a value and somewhere that value is saved, and an aspect
        // carries all four. A pane that hands over a QToolButton instead has
        // nothing a toolbar which is not a QToolBar can draw.
        const QList<IOutputPane *> panes = IOutputPane::allOutputPanes();
        QList<IOutputPane *> withToggles;
        for (IOutputPane * const pane : panes) {
            if (!pane->toolBarAspects().isEmpty())
                withToggles << pane;
        }
        QVERIFY2(withToggles.size() >= 2,
                 qPrintable(QString("only %1 pane names a toggle; the point of naming "
                                    "them is that more than one can")
                                .arg(withToggles.size())));

        // One that saves something, so the button built from it has a name
        // to be found by.
        Utils::BaseAspect *aspect = nullptr;
        for (IOutputPane * const pane : withToggles) {
            for (Utils::BaseAspect * const candidate : pane->toolBarAspects()) {
                if (!aspect && !candidate->settingsKey().isEmpty())
                    aspect = candidate;
            }
        }
        QVERIFY2(aspect, "no named toggle is saved anywhere, so none can be found by name");
        QVERIFY2(aspect->action(), "the aspect offers nothing for a toolbar to put a button on");
        QVERIFY2(aspect->action()->isCheckable(),
                 "a toggle in a toolbar that cannot be toggled");

        // And a toolbar built one from it, carrying the aspect's own state.
        // Searched for rather than indexed by the pane's row: the toolbar
        // stack does not stay in step with the pane list, which is recorded
        // in the plan as its own defect and is not what this test is about.
        const QString key = QString::fromUtf8(aspect->settingsKey().view());
        QToolButton *toggle = nullptr;
        for (int i = 0; i < m_instance->m_opToolBarWidgets->count(); ++i) {
            if (auto * const found
                = m_instance->m_opToolBarWidgets->widget(i)->findChild<QToolButton *>(key)) {
                toggle = found;
            }
        }
        QVERIFY2(toggle, qPrintable("no toolbar holds a button for " + key));
        QCOMPARE(toggle->defaultAction(), aspect->action());

        auto * const boolAspect = dynamic_cast<Utils::BoolAspect *>(aspect);
        QVERIFY(boolAspect);
        const bool was = boolAspect->value();
        const QScopeGuard restore([boolAspect, was] { boolAspect->setValue(was); });
        boolAspect->setValue(!was);
        QCOMPARE(toggle->isChecked(), !was);
    }

    void testTheButtonDrawsWhatTheModelSays()
    {
        OutputPaneButtonModel * const model = OutputPaneManager::instance()->m_buttonModel;
        QVERIFY(model);
        QVERIFY(model->rowCount() > 0);
        const int row = 0;
        // The shell's row is the Qt Quick one, so no QToolButton is there to
        // be written to. Stand one in the row's place: what is under test is
        // that the model reaches whichever button the row has. The holder
        // keeps it off screen and outlives the guard that unhooks it.
        QWidget holder;
        OutputPaneToggleButton * const existing = g_outputPanes.at(row).button;
        if (!existing) {
            auto * const standIn = new OutputPaneToggleButton(g_outputPanes.at(row).number,
                                                              g_outputPanes.at(row).pane->displayName(),
                                                              g_outputPanes.at(row).action);
            standIn->setParent(&holder);
            g_outputPanes[row].button = standIn;
        }
        const QScopeGuard putBack([existing] { g_outputPanes[row].button = existing; });
        OutputPaneToggleButton * const button = g_outputPanes.at(row).button;
        QVERIFY(button);

        const bool wasVisible = model->isButtonVisible(row);
        const QScopeGuard restore([model, wasVisible] {
            model->setButtonVisible(row, wasVisible); });

        // Whether a pane has a button at all is what the manage menu changes
        // and the session remembers. isVisibleTo(), not isVisible(): the
        // whole row is inside a window nothing has shown.
        model->setButtonVisible(row, true);
        QVERIFY(button->isVisibleTo(button->window()));
        model->setButtonVisible(row, false);
        QVERIFY2(!button->isVisibleTo(button->window()),
                 "the button stayed after the model took it away");
        model->setButtonVisible(row, true);
        QVERIFY(button->isVisibleTo(button->window()));

        // The badge is the model's too, and the button makes room for it.
        IOutputPane * const pane = IOutputPane::allOutputPanes().at(row);
        const QVariant badge = model->index(row).data(OutputPaneButtonModel::BadgeRole);
        const QScopeGuard restoreBadge([pane, badge] {
            pane->setIconBadgeNumber(badge.toString().toInt()); });
        pane->setIconBadgeNumber(0);
        const int plain = button->sizeHint().width();
        pane->setIconBadgeNumber(88);
        QVERIFY2(button->sizeHint().width() > plain,
                 qPrintable(QString("the button asks for %1 either way")
                                .arg(button->sizeHint().width())));
    }
};

QObject *createOutputPaneButtonModelTest()
{
    return new OutputPaneButtonModelTest;
}

#endif // WITH_TESTS

} // namespace Internal
} // namespace Core

#include "outputpanemanager.moc"

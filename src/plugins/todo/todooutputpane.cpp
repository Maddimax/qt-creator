// Copyright (C) 2016 Dmitry Savchenko
// Copyright (C) 2016 Vasiliy Sorokin
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "todooutputpane.h"

#include "constants.h"
#include "todoitemsmodel.h"
#include "todoitemsprovider.h"
#include "todotr.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>

#include <QIcon>
#include <QHeaderView>
#include <QToolButton>
#include <QButtonGroup>
#include <QSortFilterProxyModel>
#include <qtcquick/qtcquickwidget.h>

#include <QQuickWidget>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

namespace Todo::Internal {

TodoOutputPane::TodoOutputPane(TodoItemsModel *todoItemsModel, QObject *parent) :
    IOutputPane(parent),
    m_todoItemsModel(todoItemsModel)
{
    setId("To-DoEntries");
    setDisplayName(Tr::tr("To-Do Entries"));
    setPriorityInStatusBar(10);

    createTreeView();
    createScopeButtons();

    setScanningScope(todoSettings().scanningScope);

    connect(rows(), &QAbstractItemModel::layoutChanged,
            this, &TodoOutputPane::navigateStateUpdate);
    connect(rows(), &QAbstractItemModel::layoutChanged,
            this, &TodoOutputPane::updateTodoCount);
}

TodoOutputPane::~TodoOutputPane()
{
    freeTreeView();
    freeScopeButtons();
}

QWidget *TodoOutputPane::outputWidget(QWidget *parent)
{
    if (!m_view) {
        auto widget = new QtcQuick::QuickWidget(parent);
        widget->quickWidget()->setInitialProperties({{"pane", QVariant::fromValue(this)}});
        widget->setSource(QUrl("qrc:/qt/qml/QtCreator/Todo/TodoPane.qml"));
        m_view = widget;
    }
    return m_view;
}

QList<QWidget*> TodoOutputPane::toolBarWidgets() const
{
    QWidgetList widgets;

    for (QToolButton *btn: m_filterButtons)
        widgets << btn;

    widgets << m_spacer << m_currentFileButton << m_wholeProjectButton << m_subProjectButton;

    return widgets;
}

void TodoOutputPane::clearContents()
{
    clearKeywordFilter();
}

void TodoOutputPane::setFocus()
{
    if (m_view)
        m_view->setFocus();
}

bool TodoOutputPane::hasFocus() const
{
    return m_view && m_view->window()->focusWidget() == m_view;
}

bool TodoOutputPane::canFocus() const
{
    return true;
}

bool TodoOutputPane::canNavigate() const
{
    return true;
}

bool TodoOutputPane::canNext() const
{
    return rows()->rowCount() > 0;
}

bool TodoOutputPane::canPrevious() const
{
    return rows()->rowCount() > 0;
}

void TodoOutputPane::goToNext()
{
    const int row = nextRow();
    if (row < 0)
        return;
    setCurrentRow(row);
    activateRow(row);
}

void TodoOutputPane::goToPrev()
{
    const int row = previousRow();
    if (row < 0)
        return;
    setCurrentRow(row);
    activateRow(row);
}

void TodoOutputPane::setScanningScope(ScanningScope scanningScope)
{
    if (scanningScope == ScanningScopeCurrentFile)
        m_currentFileButton->setChecked(true);
    else if (scanningScope == ScanningScopeSubProject)
        m_subProjectButton->setChecked(true);
    else if (scanningScope == ScanningScopeProject)
        m_wholeProjectButton->setChecked(true);
    else
        Q_ASSERT_X(false, "Updating scanning scope buttons", "Unknown scanning scope enum value");
}

void TodoOutputPane::todoItemClicked(const TodoItem &item)
{
    if (item.file.exists())
        Core::EditorManager::openEditorAt(Utils::Link(item.file, item.line));
}

void TodoOutputPane::scopeButtonClicked(QAbstractButton *button)
{
    if (button == m_currentFileButton)
        scanningScopeChanged(ScanningScopeCurrentFile);
    else if (button == m_subProjectButton)
        scanningScopeChanged(ScanningScopeSubProject);
    else if (button == m_wholeProjectButton)
        scanningScopeChanged(ScanningScopeProject);
    emit setBadgeNumber(rows()->rowCount());
}

void TodoOutputPane::scanningScopeChanged(ScanningScope scanningScope)
{
    todoSettings().scanningScope = scanningScope;
    todoSettings().save();

    todoItemsProvider().settingsChanged();
    setScanningScope(todoSettings().scanningScope);
}

void TodoOutputPane::setCurrentRow(int row)
{
    if (m_currentRow == row)
        return;
    m_currentRow = row;
    emit currentRowChanged();
}

void TodoOutputPane::sortBy(int column, bool ascending)
{
    m_sortColumn = column;
    m_sortOrder = ascending ? Qt::AscendingOrder : Qt::DescendingOrder;
    m_filteredTodoItemsModel->sort(m_sortColumn, m_sortOrder);
}

void TodoOutputPane::activateRow(int row)
{
    // Create a to-do item and notify that it was clicked on
    const QModelIndex index = rows()->index(row, Constants::OUTPUT_COLUMN_TEXT);
    if (!index.isValid())
        return;

    TodoItem item;
    item.text = index.sibling(row, Constants::OUTPUT_COLUMN_TEXT).data().toString();
    item.file = Utils::FilePath::fromUserInput(index.sibling(row, Constants::OUTPUT_COLUMN_FILE).data().toString());
    item.line = index.sibling(row, Constants::OUTPUT_COLUMN_LINE).data().toInt();
    item.color = index.data(Qt::ForegroundRole).value<QColor>();
    item.iconType = static_cast<IconType>(index.sibling(row, Constants::OUTPUT_COLUMN_TEXT)
                                          .data(Qt::UserRole).toInt());

    todoItemClicked(item);
}

void TodoOutputPane::updateTodoCount()
{
    emit setBadgeNumber(rows()->rowCount());
}

void TodoOutputPane::updateKeywordFilter()
{
    QStringList keywords;
    for (const QToolButton *btn: std::as_const(m_filterButtons)) {
        if (btn->isChecked())
            keywords.append(btn->property(Constants::FILTER_KEYWORD_NAME).toString());
    }

    QString pattern = keywords.isEmpty() ? QString() : QString("^(%1).*").arg(keywords.join('|'));
    m_filteredTodoItemsModel->setFilterRegularExpression(pattern);
    // The order the list is in, which the view used to keep in its header.
    m_filteredTodoItemsModel->sort(m_sortColumn, m_sortOrder);

    updateTodoCount();
}

void TodoOutputPane::clearKeywordFilter()
{
    for (QToolButton *btn: std::as_const(m_filterButtons))
        btn->setChecked(false);

    updateKeywordFilter();
}

void TodoOutputPane::createTreeView()
{
    m_filteredTodoItemsModel = new QSortFilterProxyModel();
    m_filteredTodoItemsModel->setSourceModel(m_todoItemsModel);
    m_filteredTodoItemsModel->setDynamicSortFilter(false);
    m_filteredTodoItemsModel->setFilterKeyColumn(Constants::OUTPUT_COLUMN_TEXT);
}

QAbstractItemModel *TodoOutputPane::rows() const
{
    return m_filteredTodoItemsModel;
}

void TodoOutputPane::freeTreeView()
{
    // Not the view: whoever asked for it owns it.
    delete m_filteredTodoItemsModel;
}

QToolButton *TodoOutputPane::createCheckableToolButton(const QString &text, const QString &toolTip, const QIcon &icon)
{
    auto button = new QToolButton;

    button->setCheckable(true);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setIcon(icon);

    return button;
}

void TodoOutputPane::createScopeButtons()
{
    m_currentFileButton = new QToolButton();
    m_currentFileButton->setCheckable(true);
    m_currentFileButton->setText(Tr::tr("Current Document"));
    m_currentFileButton->setToolTip(Tr::tr("Scan only the currently edited document."));

    m_wholeProjectButton = new QToolButton();
    m_wholeProjectButton->setCheckable(true);
    m_wholeProjectButton->setText(Tr::tr("Active Project"));
    m_wholeProjectButton->setToolTip(Tr::tr("Scan the whole active project."));

    m_subProjectButton = new QToolButton();
    m_subProjectButton->setCheckable(true);
    m_subProjectButton->setText(Tr::tr("Subproject"));
    m_subProjectButton->setToolTip(Tr::tr("Scan the current subproject."));

    m_scopeButtons = new QButtonGroup();
    m_scopeButtons->addButton(m_wholeProjectButton);
    m_scopeButtons->addButton(m_currentFileButton);
    m_scopeButtons->addButton(m_subProjectButton);
    connect(m_scopeButtons, &QButtonGroup::buttonClicked,
            this, &TodoOutputPane::scopeButtonClicked);

    m_spacer = new QWidget;
    m_spacer->setMinimumWidth(Constants::OUTPUT_TOOLBAR_SPACER_WIDTH);

    QString tooltip = Tr::tr("Show \"%1\" entries");
    for (const Keyword &keyword: std::as_const(todoSettings().keywords)) {
        QToolButton *button = createCheckableToolButton(keyword.name, tooltip.arg(keyword.name), toolBarIcon(keyword.iconType));
        button->setProperty(Constants::FILTER_KEYWORD_NAME, keyword.name);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        connect(button, &QToolButton::clicked, this, &TodoOutputPane::updateKeywordFilter);

        m_filterButtons.append(button);
    }
}

void TodoOutputPane::freeScopeButtons()
{
    delete m_currentFileButton;
    delete m_wholeProjectButton;
    delete m_subProjectButton;
    delete m_scopeButtons;
    delete m_spacer;

    qDeleteAll(m_filterButtons);
}


int TodoOutputPane::nextRow() const
{
    const int count = rows()->rowCount();
    if (count == 0)
        return -1;
    return m_currentRow + 1 < count ? m_currentRow + 1 : 0;
}

int TodoOutputPane::previousRow() const
{
    const int count = rows()->rowCount();
    if (count == 0)
        return -1;
    return m_currentRow > 0 ? m_currentRow - 1 : count - 1;
}

static TodoOutputPane *s_instance = nullptr;

TodoOutputPane &todoOutputPane()
{
    return *s_instance;
}

void setupTodoOutputPane(QObject *guard)
{
    s_instance = new TodoOutputPane(todoItemsProvider().todoItemsModel(), guard);
}

#ifdef WITH_TESTS

class TodoPaneTest final : public QObject
{
    Q_OBJECT

private slots:
    // The pane used to keep its current row in a tree view's selection model,
    // so it could not say where the list was without drawing one, and the sort
    // order lived in that view's header. Both are the pane's now, and the list
    // is drawn with Qt Quick.
    void testTheListIsTheModelAndTheCurrentRowIsThePane()
    {
        TodoItem first;
        first.text = "TODO first";
        // A path that does not exist: activating a row opens the file only if
        // there is one, which keeps this test out of the editor.
        first.file = Utils::FilePath::fromString("/nonexistent/a.cpp");
        first.line = 12;
        TodoItem second;
        second.text = "FIXME second";
        second.file = Utils::FilePath::fromString("/nonexistent/b.cpp");
        second.line = 34;
        QList<TodoItem> items{first, second};

        TodoItemsModel model;
        model.setTodoItemsList(&items);
        TodoOutputPane pane(&model, nullptr);

        QCOMPARE(pane.rows()->rowCount(), 2);
        // Ordered from the start: the pane sorts when it works out its filter,
        // which it does while being built.
        QCOMPARE(pane.rows()->index(0, Constants::OUTPUT_COLUMN_TEXT).data().toString(),
                 QString("FIXME second"));

        // Walking the list is the pane's own, and wraps at both ends the way
        // the tree view's indexBelow()/indexAbove() did.
        QCOMPARE(pane.currentRow(), -1);
        QSignalSpy moved(&pane, &TodoOutputPane::currentRowChanged);
        pane.goToNext();
        QCOMPARE(pane.currentRow(), 0);
        pane.goToNext();
        QCOMPARE(pane.currentRow(), 1);
        pane.goToNext();
        QCOMPARE(pane.currentRow(), 0);
        pane.goToPrev();
        QCOMPARE(pane.currentRow(), 1);
        QCOMPARE(moved.count(), 4);

        // And the order is the pane's too, not a header's: turning it round is
        // what clicking the heading asks for.
        pane.sortBy(Constants::OUTPUT_COLUMN_TEXT, false);
        QCOMPARE(pane.rows()->index(0, Constants::OUTPUT_COLUMN_TEXT).data().toString(),
                 QString("TODO first"));
        pane.sortBy(Constants::OUTPUT_COLUMN_TEXT, true);
        QCOMPARE(pane.rows()->index(0, Constants::OUTPUT_COLUMN_TEXT).data().toString(),
                 QString("FIXME second"));

        // The model has to be readable by name or the list draws nothing: the
        // text, the icon and the colour a to-do is drawn in.
        const QList<QByteArray> names = pane.rows()->roleNames().values();
        for (const QByteArray &role : {QByteArray("display"), QByteArray("decoration"),
                                       QByteArray("foreground")}) {
            QVERIFY2(names.contains(role),
                     qPrintable("the model does not answer " + QString::fromLatin1(role)));
        }

        // Drawn with Qt Quick, and showing both entries.
        const std::unique_ptr<QWidget> widget(pane.outputWidget(nullptr));
        QVERIFY(widget);
        auto * const quick = qobject_cast<QtcQuick::QuickWidget *>(widget.get());
        QVERIFY2(quick, "the pane is not drawn with Qt Quick");
        widget->resize(600, 300);
        widget->show();
        // Shown, but not waited on for exposure: this window reports itself
        // exposed unreliably here, and what the test is after is the rows,
        // which is a signal of its own.


        QObject * const root = quick->rootObject();
        QVERIFY(root);
        QObject * const list = root->findChild<QObject *>("todoList");
        QVERIFY2(list, "the pane draws no list");
        QTRY_COMPARE(list->property("rows").toInt(), 2);
        QVERIFY2(root->findChild<QObject *>("todoHeader"), "the list has no header to sort by");
    }
};

QObject *createTodoPaneTest()
{
    return new TodoPaneTest;
}

#endif // WITH_TESTS

} // Todo::Internal

#ifdef WITH_TESTS
#include "todooutputpane.moc"
#endif

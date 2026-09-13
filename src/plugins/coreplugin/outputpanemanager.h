// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/id.h>

#include <QAbstractListModel>
#include <QToolButton>

QT_BEGIN_NAMESPACE
class QAction;
class QLabel;
class QMenu;
class QStackedWidget;
QT_END_NAMESPACE

namespace Core {

class ICore;
class IOutputPane;

namespace Internal {

class ICorePrivate;
class MainWindow;

// The row of output-pane buttons, as a model: one row per pane, in the order
// the status bar shows them, which is by priority in the status bar.
//
// The row's state used to live in the QToolButtons themselves. *Which* pane
// has a button at all is remembered across sessions, and was written by asking
// a widget whether it was visible - so a row drawn any other way had nowhere
// to keep it. It is kept here now, and the buttons are a view of it.
class OutputPaneButtonModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        NumberRole = Qt::UserRole, // The Alt+<n> that reaches this pane.
        BadgeRole,                 // What the pane has to report, empty for nothing.
        CheckedRole,
        ButtonVisibleRole,
    };

    explicit OutputPaneButtonModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const final;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const final;
    // Named so that a QML row can read them; a widget button asks by number.
    QHash<int, QByteArray> roleNames() const final;

    // Whether this pane has a button in the status bar at all: the one bit of
    // this row the session remembers.
    bool isButtonVisible(int row) const;
    void setButtonVisible(int row, bool visible);

    void setChecked(int row, bool checked);
    void setBadge(int row, int number);

    // The tooltip is the command's to say - it names the keys that reach the
    // pane - so the row is told rather than asked.
    void toolTipChanged(int row);

    // A pane asking to be noticed. An event rather than state, so it is a
    // signal and not a role.
    void requestFlash(int row);

    // What pressing a row does, and the menu the row itself offers - the same
    // two things the buttons are wired to.
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void showMenu();

    // The panes are collected once, but the buttons are built from scratch
    // when they are.
    void reset();

signals:
    void flashRequested(int row);
};

// What the Qt Quick row needs of the buttons above. One per row, and short
// lived: Core::createQmlView() hands the scene the controller it is given and
// parents it to the widget that comes back, while the model belongs to the
// output pane manager and outlives every row. So this stands between them.
class OutputPaneButtons : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)

public:
    explicit OutputPaneButtons(OutputPaneButtonModel *model, QObject *parent = nullptr);

    QAbstractItemModel *model() const;

    // A row pressed, and the menu that says which panes have buttons at all.
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void showMenu();

signals:
    // Relayed, because a delegate cannot connect to the model's own signal
    // without reaching past its controller.
    void flashRequested(int row);

private:
    OutputPaneButtonModel *m_model = nullptr;
};

class OutputPaneManager : public QWidget
{
    Q_OBJECT

public:
    static OutputPaneManager *instance();
    // The row of buttons in Qt Quick, for whoever asks: the shell when its
    // switch is set, and a test that wants one of its own.
    static QWidget *createButtonRow();
    void updateStatusButtons(bool visible);
    static void updateMaximizeButton(bool maximized);

    static int outputPaneHeightSetting();
    static void setOutputPaneHeightSetting(int value);
    static bool initialized();

public slots:
    void slotHide();
    void slotNext();
    void slotPrev();
    static void toggleMaximized();

protected:
    void focusInEvent(QFocusEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    // the only class that is allowed to create and destroy
    friend class Core::ICore;
    friend class ICorePrivate;
    friend class MainWindow;
    friend class OutputPaneButtonModel;
    friend class Core::IOutputPane;
#ifdef WITH_TESTS
    friend class OutputPaneButtonModelTest;
#endif

    explicit OutputPaneManager(QWidget *parent = nullptr);
    ~OutputPaneManager() override;

    static void create();
    static void initialize();
    static void setupButtons();
    // The one place a pane's button is written to, from the model's row.
    static void destroy();

    void shortcutTriggered(int idx);
    void clearPage();
    void fillManageMenu(QMenu *menu);
    void popupMenu();
    void saveSettings() const;
    void showPage(int idx, int flags);
    void ensurePageVisible(int idx);
    int currentIndex() const;
    void setCurrentIndex(int idx);
    void buttonTriggered(int idx);
    void readSettings();
    void updateActions(IOutputPane *pane);

    OutputPaneButtonModel *m_buttonModel = nullptr;
    QLabel *m_titleLabel = nullptr;

    QAction *m_clearAction = nullptr;
    QAction *m_closeAction = nullptr;
    QAction *m_minMaxAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_prevAction = nullptr;

    QStackedWidget *m_outputWidgetPane = nullptr;
    QStackedWidget *m_opToolBarWidgets = nullptr;
    QWidget *m_buttonsWidget = nullptr;
    QWidget *m_quickButtonRow = nullptr;
    int m_outputPaneHeightSetting = 0;
    bool m_initialized = false;
};

#ifdef WITH_TESTS
QObject *createOutputPaneButtonModelTest();
#endif

} // namespace Internal
} // namespace Core

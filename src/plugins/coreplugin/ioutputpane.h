// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <utils/fancylineedit.h>
#include <utils/aspects.h>
#include <utils/id.h>

namespace Core {

#ifdef WITH_TESTS
namespace Internal { class OutputPaneButtonModelTest; }
#endif

class Context;

class CORE_EXPORT IOutputPane : public QObject
{
    Q_OBJECT

public:
    IOutputPane(QObject *parent = nullptr);
    ~IOutputPane() override;

    static const QList<IOutputPane *> allOutputPanes();

    virtual QWidget *outputWidget(QWidget *parent) = 0;
    virtual QList<QWidget *> toolBarWidgets() const;

    // The commands this pane's toolbar offers, named rather than built. Every
    // pane gets the two zoom ones, and they used to be a pair of QToolButtons
    // per pane - thirteen pairs of the same two buttons, which is thirteen
    // widgets a toolbar that is not a QToolBar would have to host.
    virtual QList<Utils::Id> toolBarCommands() const;
    // One entry of a pane's toolbar, in the order the pane wants them. The
    // three lists above cannot express that between them: a pane's own
    // widgets, its toggles and its commands interleave - Console's toggles
    // come before its spacer, and Test Results' duration toggle sits seventh
    // of nine - and a fixed order silently rearranges somebody's toolbar.
    class CORE_EXPORT ToolBarItem
    {
    public:
        static ToolBarItem forWidget(QWidget *widget) { return {widget, nullptr, {}}; }
        static ToolBarItem forAspect(Utils::BaseAspect *aspect) { return {nullptr, aspect, {}}; }
        static ToolBarItem forCommand(Utils::Id command) { return {nullptr, nullptr, command}; }

        QWidget *widget() const { return m_widget; }
        Utils::BaseAspect *aspect() const { return m_aspect; }
        Utils::Id command() const { return m_command; }

    private:
        ToolBarItem(QWidget *widget, Utils::BaseAspect *aspect, Utils::Id command)
            : m_widget(widget), m_aspect(aspect), m_command(command) {}

        QWidget *m_widget = nullptr;
        Utils::BaseAspect *m_aspect = nullptr;
        Utils::Id m_command;
    };

    // What the toolbar draws. The default puts the three lists above in the
    // order they used to be assembled in, so a pane that has not been given
    // an order keeps the one it had.
    virtual QList<ToolBarItem> toolBarItems() const;


    // The settings this pane's toolbar toggles. An aspect already carries its
    // icon, its tooltip, its value and where that value is saved, and this
    // branch draws aspects in either toolkit - so a pane says which ones
    // rather than building a QToolButton around each.
    virtual QList<Utils::BaseAspect *> toolBarAspects() const;
    Utils::Id id() const;
    QString displayName() const;
    // The text this pane shows, one entry per view it holds. Empty for a pane
    // that is not a plain-text one, which is what tells a reader to say so
    // rather than to report a pane as empty.
    virtual QStringList outputTexts() const { return {}; }

    // Whether this pane can show where a task was reported, and doing so.
    // Panes hold their views in their own way - one, or one per run - so
    // finding the right one is theirs to do rather than a caller's.
    virtual bool canShowPositionOf(unsigned taskId) const { Q_UNUSED(taskId) return false; }
    virtual void showPositionOf(unsigned taskId) { Q_UNUSED(taskId) }

    int priorityInStatusBar() const;

    virtual void clearContents() = 0;
    virtual void visibilityChanged(bool visible);

    virtual void setFocus() = 0;
    virtual bool hasFocus() const = 0;
    virtual bool canFocus() const = 0;

    virtual bool canNavigate() const = 0;
    virtual bool canNext() const = 0;
    virtual bool canPrevious() const = 0;
    virtual void goToNext() = 0;
    virtual void goToPrev() = 0;

    virtual bool hasFilterContext() const;

    void setFont(const QFont &font);
    void setWheelZoomEnabled(bool enabled);

    enum Flag { NoModeSwitch = 0, ModeSwitch = 1, WithFocus = 2, EnsureSizeHint = 4};
    Q_DECLARE_FLAGS(Flags, Flag)

public slots:
    void popup(int flags) { emit showPage(flags); }

    void hide() { emit hidePage(); }
    void toggle(int flags) { emit togglePage(flags); }
    void navigateStateChanged() { emit navigateStateUpdate(); }
    void flash() { emit flashButton(); }
    void setIconBadgeNumber(int number) { emit setBadgeNumber(number); }

    // Whether zooming applies to what this pane is showing. A pane used to
    // reach into two buttons it owned; the toolbar reads this instead.
    bool zoomEnabled() const { return m_zoomEnabled; }

signals:
    void zoomEnabledChanged(bool enabled);

    void showPage(int flags);
    void hidePage();
    void togglePage(int flags);
    void navigateStateUpdate();
    void flashButton();
    void setBadgeNumber(int number);
    void zoomInRequested(int range);
    void zoomOutRequested(int range);
    void resetZoomRequested();
    void wheelZoomEnabledChanged(bool enabled);
    void fontChanged(const QFont &font);

protected:
    void setId(const Utils::Id &id);
    void setDisplayName(const QString &name);
    void setPriorityInStatusBar(int priority);

    void setupFilterUi(const Utils::Key &historyKey, const QString &actionSuffix);
    QString filterText() const;
    bool filterUsesRegexp() const { return m_filterRegexp; }
    bool filterIsInverted() const { return m_invertFilter; }
    int beforeContext() const { return m_beforeContext; }
    int afterContext() const { return m_afterContext; }
    Qt::CaseSensitivity filterCaseSensitivity() const { return m_filterCaseSensitivity; }
    void setFilteringEnabled(bool enable);
    QWidget *filterWidget() const { return m_filterOutputLineEdit; }
    void setupContext(const Utils::Id &context, QWidget *widget);
    void setupContext(const Context &context, QWidget *widget);
    void setZoomButtonsEnabled(bool enabled);

private:
    virtual void updateFilter();

    void filterOutputButtonClicked();
    void setCaseSensitive(bool caseSensitive);
    void setRegularExpressions(bool regularExpressions);
    Utils::Id filterRegexpActionId() const;
    Utils::Id filterCaseSensitivityActionId() const;
    Utils::Id filterInvertedActionId() const;
    Utils::Id filterBeforeActionId() const;
    Utils::Id filterAfterActionId() const;

    Utils::Id m_id;
    QString m_displayName;
    int m_priority = -1;
    QString m_filterActionSuffix;
#ifdef WITH_TESTS
    friend class Internal::OutputPaneButtonModelTest;
#endif
    bool m_zoomEnabled = true;
    QAction *m_filterActionRegexp = nullptr;
    QAction *m_filterActionCaseSensitive = nullptr;
    QAction *m_invertFilterAction = nullptr;
    Utils::FancyLineEdit *m_filterOutputLineEdit = nullptr;
    bool m_filterRegexp = false;
    bool m_invertFilter = false;
    int m_beforeContext = 0;
    int m_afterContext = 0;
    Qt::CaseSensitivity m_filterCaseSensitivity = Qt::CaseInsensitive;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(IOutputPane::Flags)

} // namespace Core

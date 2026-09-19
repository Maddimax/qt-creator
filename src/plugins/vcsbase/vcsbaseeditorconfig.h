// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vcsbase_global.h"

#include <texteditor/texteditor.h>

#include <QVariant>

QT_BEGIN_NAMESPACE
class QAction;
class QStandardItemModel;
QT_END_NAMESPACE

namespace Utils {
class BoolAspect;
class IntegerAspect;
class StringAspect;
} // Utils

namespace VcsBase {

class VcsBaseEditorConfig;

namespace Internal { class VcsBaseEditorConfigPrivate; }

// One option with several values - which moves a blame detects, which format
// a log has - as a choice whichever view draws the config can show as a combo
// box. A TextEditor::ToolBarChoice, so that the Qt Quick tool bar draws it the
// way it draws a language's own choice.
class VCSBASE_EXPORT VcsBaseEditorChoice final : public TextEditor::ToolBarChoice
{
    Q_OBJECT

public:
    QAbstractItemModel *model() const final;
    int currentIndex() const final;
    QString toolTip() const final;
    bool isAvailable() const final;
    bool isChosen() const final;
    // Picks and announces: the config's arguments have changed.
    void choose(int index) final;
    void clearChoice() final;

    int count() const;
    QVariant currentValue() const;
    int indexOfValue(const QVariant &value) const;
    // Sets without announcing a change of arguments: what a setting says
    // before anything has run.
    void setCurrentIndex(int index);

private:
    friend class VcsBaseEditorConfig;
    VcsBaseEditorChoice(VcsBaseEditorConfig *config, const QString &title,
                        const QList<QVariant> &values, const QStringList &texts);

    VcsBaseEditorConfig *const m_config;
    QStandardItemModel *const m_model;
    const QString m_title;
    const QList<QVariant> m_values;
    int m_current = 0;
};

// What a VCS command can be told, as toggles and choices, and the arguments
// they amount to. Says nothing about how it is drawn: a view asks actions()
// and choices() - the widget editor gets the actions through its document's
// tool bar actions and makes a combo box per choice; the Qt Quick tool bar
// draws both from the same lists.
class VCSBASE_EXPORT VcsBaseEditorConfig : public QObject
{
    Q_OBJECT

public:
    explicit VcsBaseEditorConfig(QObject *parent = nullptr);
    ~VcsBaseEditorConfig() override;

    class VCSBASE_EXPORT ChoiceItem
    {
    public:
        ChoiceItem() = default;
        ChoiceItem(const QString &text, const QVariant &val);
        QString displayText;
        QVariant value;
    };

    QStringList baseArguments() const;
    void setBaseArguments(const QStringList &);

    QAction *addReloadButton();
    QAction *addToggleButton(const QString &option, const QString &label,
                             const QString &tooltip = {});
    QAction *addToggleButton(const QStringList &options, const QString &label,
                             const QString &tooltip = {});
    VcsBaseEditorChoice *addChoices(const QString &title,
                                    const QStringList &options,
                                    const QList<ChoiceItem> &items);
    // An action of the VCS's own beside the toggles, which says nothing about
    // the arguments.
    void addAction(QAction *action);

    void mapSetting(QAction *button, Utils::BoolAspect *setting);
    void mapSetting(VcsBaseEditorChoice *choice, Utils::StringAspect *setting);
    void mapSetting(VcsBaseEditorChoice *choice, Utils::IntegerAspect *setting);

    // What a view draws, in the order it was added.
    QList<QAction *> actions() const;
    QList<VcsBaseEditorChoice *> choices() const;

    // Return the effective arguments according to setting.
    virtual QStringList arguments() const;

    void handleArgumentsChanged();
    void executeCommand();

signals:
    void commandExecutionRequested();

    // Trigger a re-run to show changed output according to new argument list.
    void argumentsChanged();

protected:
    class OptionMapping
    {
    public:
        OptionMapping() = default;
        OptionMapping(const QStringList &optionList, QObject *obj);
        QStringList options;
        QObject *object = nullptr;
    };

    const QList<OptionMapping> &optionMappings() const;
    virtual QStringList argumentsForOption(const OptionMapping &mapping) const;
    void updateMappedSettings();

private:
    friend class Internal::VcsBaseEditorConfigPrivate;
    Internal::VcsBaseEditorConfigPrivate *const d;
};

} // namespace VcsBase

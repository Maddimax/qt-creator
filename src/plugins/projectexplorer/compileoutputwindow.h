// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "buildstep.h"

#include "appoutputpane.h"

#include <coreplugin/ioutputpane.h>

#include <QCoreApplication>

QT_BEGIN_NAMESPACE
class QToolButton;
QT_END_NAMESPACE

namespace Core { class OutputPaneView; }
namespace Utils { class OutputFormatter; }

namespace ProjectExplorer {
class Task;

namespace Internal {
class ShowOutputTaskHandler;

class CompileOutputSettings final : public Utils::AspectContainer
{
public:
    CompileOutputSettings();

    Utils::BoolAspect popUp{this};
    Utils::BoolAspect wrapOutput{this};
    Utils::BoolAspect discardOutput{this};
    Utils::BoolAspect overwriteColor{this};
    OutputMaxCharCountAspect maxCharCount{this};
    OutputColorAspect backgroundColor{this};
};

CompileOutputSettings &compileOutputSettings();

class CompileOutputWindow final : public Core::IOutputPane
{
    Q_OBJECT

public:
    explicit CompileOutputWindow(QAction *cancelBuildAction);
    ~CompileOutputWindow() override;

    QWidget *outputWidget(QWidget *) override;
    QList<ToolBarItem> toolBarItems() const override;
    void clearContents() override;
    bool canFocus() const override;
    bool hasFocus() const override;
    void setFocus() override;

    bool canNext() const override;
    bool canPrevious() const override;
    void goToNext() override;
    void goToPrev() override;
    bool canNavigate() const override;

    bool hasFilterContext() const override;

    void appendText(const QString &text, BuildStep::OutputFormat format);

    void registerPositionOf(const Task &task, int linkedOutputLines, int skipLines, int offset = 0);

    void flush();
    void reset();

    Utils::OutputFormatter *outputFormatter() const;

private:
    void updateFilter() override;
    QStringList outputTexts() const override;
    bool canShowPositionOf(unsigned taskId) const override;
    void showPositionOf(unsigned taskId) override;

    void updateFromSettings();
    Core::OutputPaneView *m_outputWindow;
    ShowOutputTaskHandler *m_handler;
    QAction *m_cancelBuildAction = nullptr;
    QAction *m_settingsAction = nullptr;
};

} // namespace Internal
} // namespace ProjectExplorer

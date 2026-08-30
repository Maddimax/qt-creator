// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"

#include <coreplugin/outputpaneview.h>

#include <utils/outputformat.h>

#include <QAction>
#include <QPointer>
#include <QWidget>

namespace Utils { class FancyLineEdit; }

namespace ProjectExplorer::Internal {

// The view plus the commands that drive it: clear, the filter line edit and
// its three options, and the two zoom steps. Registers actions under fixed
// ids, so there is exactly one of these.
class BuildSystemOutputWindow : public QWidget
{
    Q_OBJECT

public:
    BuildSystemOutputWindow();

    QWidget *toolBar();

    void appendMessage(const QString &text, Utils::OutputFormat format);
    void grayOutOldContent();
    void clearLinesPrefixedWith(const QString &prefix, bool deleteTrailingLineBreak);

private:
    void updateFilter();

    Core::OutputPaneView *m_view = nullptr;
    QPointer<QWidget> m_toolBar;
    QPointer<Utils::FancyLineEdit> m_filterOutputLineEdit;
    QAction m_clear;
    QAction m_filterActionRegexp;
    QAction m_filterActionCaseSensitive;
    QAction m_invertFilterAction;
    QAction m_zoomIn;
    QAction m_zoomOut;
};

} // namespace ProjectExplorer::Internal

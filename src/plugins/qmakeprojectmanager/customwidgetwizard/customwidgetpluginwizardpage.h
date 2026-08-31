// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "filenamingparameters.h"

#include <QWizardPage>

#include <memory>

namespace QmakeProjectManager::Internal {

struct PluginOptions;
class CustomWidgetPluginAspects;
class CustomWidgetWidgetsWizardPage;

class CustomWidgetPluginWizardPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit CustomWidgetPluginWizardPage(QWidget *parent = nullptr);
    ~CustomWidgetPluginWizardPage() override;

    void init(const CustomWidgetWidgetsWizardPage *widgetsPage);

    bool isComplete() const override;

    FileNamingParameters fileNamingParameters() const { return m_fileNamingParameters; }
    void setFileNamingParameters(const FileNamingParameters &fnp) {m_fileNamingParameters = fnp; }

    // Fills the plugin fields, excluding widget list.
    std::shared_ptr<PluginOptions> basicPluginOptions() const;

#ifdef WITH_TESTS
    CustomWidgetPluginAspects *aspectsForTest() const { return d.get(); }
#endif

private:
    FileNamingParameters m_fileNamingParameters;
    const std::unique_ptr<CustomWidgetPluginAspects> d;
};

// Whether the page can be left: a plugin needs a name, and a collection of
// more than one widget needs a class to collect them in.
bool pluginPageIsComplete(const QString &pluginName, const QString &collectionClass,
                          int classCount);

#ifdef WITH_TESTS
QObject *createCustomWidgetPluginPageTest();
#endif

} // namespace QmakeProjectManager::Internal

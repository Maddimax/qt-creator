// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "pluginoptions.h"
#include "filenamingparameters.h"

#include <QList>
#include <QWizardPage>

#include <memory>

namespace QmakeProjectManager::Internal {

class CustomWidgetClassesAspects;
struct PluginOptions;

class CustomWidgetWidgetsWizardPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit CustomWidgetWidgetsWizardPage(QWidget *parent = nullptr);
    ~CustomWidgetWidgetsWizardPage() override;

    QList<PluginOptions::WidgetOptions> widgetOptions() const;

    bool isComplete() const override;

    FileNamingParameters fileNamingParameters() const { return m_fileNamingParameters; }
    void setFileNamingParameters(const FileNamingParameters &fnp) {m_fileNamingParameters = fnp; }

    int classCount() const;
    QString classNameAt(int i) const;

    void initializePage() override;

#ifdef WITH_TESTS
    CustomWidgetClassesAspects *aspectsForTest() const { return d.get(); }
#endif

private:
    FileNamingParameters m_fileNamingParameters;
    const std::unique_ptr<CustomWidgetClassesAspects> d;
};

// The .pro or .pri a widget's project file is, from the library name and
// whether the widget is linked or included.
QString widgetProjectFileName(const QString &library, bool linkLibrary);

// What a widget's dom XML says before anyone edits it.
QString xmlFromClassName(const QString &name);

#ifdef WITH_TESTS
QObject *createCustomWidgetWidgetsPageTest();
#endif

} // namespace QmakeProjectManager::Internal

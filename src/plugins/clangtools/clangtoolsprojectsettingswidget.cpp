// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangtoolsprojectsettingswidget.h"

#include "clangtool.h"
#include "clangtoolsconstants.h"
#include "clangtoolssettings.h"
#include "clangtoolstr.h"

#include <coreplugin/icore.h>

#include <projectexplorer/projectpanelfactory.h>

#include <utils/aspects.h>
#include <utils/qtcassert.h>

#include <QAbstractTableModel>

using namespace ProjectExplorer;

namespace ClangTools::Internal {

class SuppressedDiagnosticsModel : public QAbstractTableModel
{
public:
    SuppressedDiagnosticsModel(QObject *parent = nullptr) : QAbstractTableModel(parent) { }

    void setDiagnostics(const SuppressedDiagnosticsList &diagnostics);
    SuppressedDiagnostic diagnosticAt(int i) const;

    // Public like the rest of the model interface: what holds this model asks
    // it how many rows there are.
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex & = QModelIndex()) const override { return ColumnLast + 1; }
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // A Qt Quick view reads a cell by role name and cannot see flags(). Without
    // this the rows would offer themselves for editing, because a cell that
    // cannot find "editable" assumes it is.
    QHash<int, QByteArray> roleNames() const override
    {
        return Utils::AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

private:
    enum Columns { ColumnFile, ColumnDescription, ColumnLast = ColumnDescription };

    SuppressedDiagnosticsList m_diagnostics;
};

// The rows of suppressed diagnostics, and which one is current. A table aspect
// rather than a view: both backends draw it from the model, and which row is
// current is the aspect's answer so that the buttons beside it can be enabled
// from the container rather than from a selection model.
class SuppressedDiagnosticsAspect final : public Utils::BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = BaseAspect::presentation();
        p.control = Utils::AspectControls::Table;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

    void setDiagnostics(const SuppressedDiagnosticsList &diagnostics)
    {
        m_model.setDiagnostics(diagnostics);
        // The rows are gone and the one that was current with them.
        setCurrentRow(-1);
        emit rowsChanged();
    }

    Q_INVOKABLE void setCurrentRow(int row)
    {
        if (m_currentRow == row)
            return;
        m_currentRow = row;
        emit rowsChanged();
    }

    bool hasCurrent() const { return m_currentRow >= 0 && m_currentRow < m_model.rowCount(); }
    bool isEmpty() const { return m_model.rowCount() == 0; }
    SuppressedDiagnostic currentDiagnostic() const { return m_model.diagnosticAt(m_currentRow); }

signals:
    void rowsChanged();

private:
    // Parented: a model handed to QML with no parent belongs to the engine.
    SuppressedDiagnosticsModel m_model{this};
    int m_currentRow = -1;
};

// What the panel shows. The flag is kept out of the settings container because
// the run settings are disabled as a whole while the global ones are in use.
// See ProjectCommentsPanel.
class ClangToolsProjectPanel final : public Utils::AspectContainer
{
public:
    explicit ClangToolsProjectPanel(Project *project)
        : m_projectSettings(clangToolsProjectSettings(project))
    {
        // Before registering: insertAspect() forces the container's own
        // auto-apply onto what it takes in.
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ClangTools/ClangToolsProjectPanel.qml"));

        m_projectSettings->useGlobalSettings.setQmlName("UseGlobalSettings");
        registerAspect(&m_projectSettings->useGlobalSettings);

        m_restoreGlobal.setQmlName("RestoreGlobal");
        m_restoreGlobal.setActionText(Tr::tr("Restore Global Settings"));
        m_restoreGlobal.setAction([this] { restoreGlobal(); });
        registerAspect(&m_restoreGlobal);

        // Two links rather than two buttons, as the widget form had them.
        m_goToTools.setQmlName("GoToTools");
        m_goToTools.setTextFormat(Utils::AspectControls::TextFormat::RichText);
        m_goToTools.setText("<a href=\"tidy\">" + Tr::tr("Go to Clang-Tidy") + "</a>&nbsp;&nbsp;"
                            + "<a href=\"clazy\">" + Tr::tr("Go to Clazy") + "</a>");
        connect(&m_goToTools, &Utils::TextDisplay::linkActivated, this, [](const QString &link) {
            if (link == "tidy")
                clangTidyTool()->selectPerspective();
            else
                clazyTool()->selectPerspective();
        });
        registerAspect(&m_goToTools);

        m_projectSettings->setQmlName("Settings");
        registerAspect(m_projectSettings.get());

        m_suppressed.setQmlName("SuppressedDiagnostics");
        m_suppressed.setLabelText(Tr::tr("Suppressed diagnostics:"));
        registerAspect(&m_suppressed);

        m_removeSelected.setQmlName("RemoveSelected");
        m_removeSelected.setActionText(Tr::tr("Remove Selected"));
        m_removeSelected.setAction([this] { removeSelected(); });
        registerAspect(&m_removeSelected);

        m_removeAll.setQmlName("RemoveAll");
        m_removeAll.setActionText(Tr::tr("Remove All"));
        m_removeAll.setAction([this] { m_projectSettings->removeAllSuppressedDiagnostics(); });
        registerAspect(&m_removeAll);

        // Behaviour, not layout.
        m_suppressed.setDiagnostics(m_projectSettings->suppressedDiagnostics());
        connect(m_projectSettings.get(), &ClangToolsProjectSettings::suppressedDiagnosticsChanged,
                this, [this] {
                    m_suppressed.setDiagnostics(m_projectSettings->suppressedDiagnostics());
                });
        connect(&m_suppressed, &SuppressedDiagnosticsAspect::rowsChanged,
                this, [this] { updateRemoveButtons(); });
        updateRemoveButtons();

        updateForUseGlobal();
        m_projectSettings->useGlobalSettings.addOnChanged(this, [this] { updateForUseGlobal(); });
        connect(ClangToolsSettings::instance(), &ClangToolsSettings::changed,
                this, [this] { updateForUseGlobal(); });

        // A custom diagnostic configuration is the global settings' to keep,
        // whichever page it was made on.
        connect(&m_projectSettings->diagnosticConfigId, &Utils::BaseAspect::changed, this, [this] {
            ClangToolsSettings::instance()->diagnosticConfigId.setCustomConfigs(
                m_projectSettings->diagnosticConfigId.customConfigs());
            ClangToolsSettings::instance()->writeSettings();
        });
    }

    static Utils::Key extraDataKey() { return "ClangToolsProjectPanel"; }

private:
    void updateForUseGlobal()
    {
        const bool useGlobal = m_projectSettings->useGlobalSettings();
        m_projectSettings->setRunSettingsEnabled(!useGlobal);
        m_restoreGlobal.setEnabled(!useGlobal);
    }

    void updateRemoveButtons()
    {
        m_removeSelected.setEnabled(m_suppressed.hasCurrent());
        m_removeAll.setEnabled(!m_suppressed.isEmpty());
    }

    void restoreGlobal()
    {
        const ClangToolsSettings *global = ClangToolsSettings::instance();
        m_projectSettings->diagnosticConfigId.setValue(global->safeDiagnosticConfigId());
        m_projectSettings->parallelJobs.setValue(global->parallelJobs());
        m_projectSettings->preferConfigFile.setValue(global->preferConfigFile());
        m_projectSettings->buildBeforeAnalysis.setValue(global->buildBeforeAnalysis());
        m_projectSettings->analyzeOpenFiles.setValue(global->analyzeOpenFiles());
    }

    void removeSelected()
    {
        QTC_ASSERT(m_suppressed.hasCurrent(), return);
        m_projectSettings->removeSuppressedDiagnostic(m_suppressed.currentDiagnostic());
    }

    ClangToolsProjectSettings::ClangToolsProjectSettingsPtr const m_projectSettings;
    Utils::ActionAspect m_restoreGlobal;
    Utils::TextDisplay m_goToTools;
    SuppressedDiagnosticsAspect m_suppressed;
    Utils::ActionAspect m_removeSelected;
    Utils::ActionAspect m_removeAll;
};

static ClangToolsProjectPanel *clangToolsProjectPanel(Project *project)
{
    const Utils::Key key = ClangToolsProjectPanel::extraDataKey();
    QVariant v = project->extraData(key);
    if (v.isNull()) {
        v = QVariant::fromValue(new ClangToolsProjectPanel(project));
        project->setExtraData(key, v);
    }
    return v.value<ClangToolsProjectPanel *>();
}

void SuppressedDiagnosticsModel::setDiagnostics(const SuppressedDiagnosticsList &diagnostics)
{
    beginResetModel();
    m_diagnostics = diagnostics;
    endResetModel();
}

SuppressedDiagnostic SuppressedDiagnosticsModel::diagnosticAt(int i) const
{
    return m_diagnostics.at(i);
}

int SuppressedDiagnosticsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_diagnostics.count();
}

QVariant SuppressedDiagnosticsModel::headerData(int section, Qt::Orientation orientation,
                                                int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section == ColumnFile)
            return Tr::tr("File");
        if (section == ColumnDescription)
            return Tr::tr("Diagnostic");
    }
    return QVariant();
}

QVariant SuppressedDiagnosticsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return QVariant();
    // Removed, never edited.
    if (role == Utils::AspectTable::EditableRole)
        return false;
    if (role != Qt::DisplayRole)
        return QVariant();
    const SuppressedDiagnostic &diag = m_diagnostics.at(index.row());
    if (index.column() == ColumnFile)
        return diag.filePath.toUserOutput();
    if (index.column() == ColumnDescription)
        return diag.description;
    return QVariant();
}

class ClangToolsProjectPanelFactory final : public ProjectPanelFactory
{
public:
    ClangToolsProjectPanelFactory()
    {
        setPriority(100);
        setId(Constants::PROJECT_PANEL_ID);
        setDisplayName(Tr::tr("Clang Tools"));
        setSettingsProvider([](Project *project) {
            return clangToolsProjectPanel(project);
        });
    }
};

void setupClangToolsProjectPanel()
{
    static ClangToolsProjectPanelFactory theClangToolsProjectPanelFactory;
}

} // namespace ClangTools::Internal

#include "clangtoolsprojectsettingswidget.moc"

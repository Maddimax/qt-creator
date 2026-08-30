// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "memcheckerrorview.h"

#include "valgrindsettings.h"
#include "valgrindtr.h"
#include "xmlprotocol/error.h"
#include "xmlprotocol/errorlistmodel.h"
#include "xmlprotocol/frame.h"
#include "xmlprotocol/stack.h"
#include "xmlprotocol/suppression.h"

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>

#include <utils/filedialogs.h>
#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/icon.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/theme/theme.h>

#include <QAction>
#include <coreplugin/dialogs/ioptionspage.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>

using namespace Utils;
using namespace Valgrind::XmlProtocol;

namespace Valgrind::Internal {

class SuppressionSettings;

// Whether the suppression may be saved: somewhere to write it, and something
// to write. A suppression of only whitespace would suppress nothing.
bool canSaveSuppression(bool fileIsValid, const QString &suppression);

class SuppressionDialog : public QDialog
{
public:
    SuppressionDialog(MemcheckErrorView *view, const QList<XmlProtocol::Error> &errors);
    ~SuppressionDialog() override;

private:
    void validate();
    void accept() override;
    void reject() override;

    MemcheckErrorView *m_view;
    bool m_cleanupIfCanceled;
    QList<XmlProtocol::Error> m_errors;

    const std::unique_ptr<SuppressionSettings> m_settings;
    QDialogButtonBox *m_buttonBox;
};

MemcheckErrorView::MemcheckErrorView(QWidget *parent)
    : ProjectExplorer::DetailedErrorView(parent)
{
    m_suppressAction = new QAction(this);
    m_suppressAction->setText(Tr::tr("Suppress Error"));
    const QIcon icon = Icon({
            {":/utils/images/eye_open.png", Theme::TextColorNormal},
            {":/valgrind/images/suppressoverlay.png", Theme::IconsErrorColor}},
            Icon::Tint | Icon::PunchEdges).icon();
    m_suppressAction->setIcon(icon);
    m_suppressAction->setShortcuts({QKeySequence::Delete, QKeySequence::Backspace});
    m_suppressAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_suppressAction, &QAction::triggered, this, &MemcheckErrorView::suppressError);
    addAction(m_suppressAction);
}

MemcheckErrorView::~MemcheckErrorView() = default;

void MemcheckErrorView::setDefaultSuppressionFile(const FilePath &suppFile)
{
    m_defaultSuppFile = suppFile;
}

FilePath MemcheckErrorView::defaultSuppressionFile() const
{
    return m_defaultSuppFile;
}

// slot, can (for now) be invoked either when the settings were modified *or* when the active
// settings object has changed.
void MemcheckErrorView::settingsChanged(ValgrindSettings *settings)
{
    QTC_ASSERT(settings, return);
    m_settings = settings;
}

void MemcheckErrorView::suppressError()
{
    QModelIndexList indices = selectionModel()->selectedRows();
    // Can happen when using arrow keys to navigate and shortcut to trigger suppression:
    if (indices.isEmpty() && selectionModel()->currentIndex().isValid())
        indices.append(selectionModel()->currentIndex());

    QList<XmlProtocol::Error> errors;
    for (const QModelIndex &index : std::as_const(indices)) {
        Error error = model()->data(index, ErrorListModel::ErrorRole).value<Error>();
        if (!error.suppression().isNull())
            errors.append(error);
    }

    if (errors.isEmpty())
        return;

    SuppressionDialog dialog(this, errors);
    dialog.exec();
}

QList<QAction *> MemcheckErrorView::customActions() const
{
    QList<QAction *> actions;
    const QModelIndexList indizes = selectionModel()->selectedRows();
    QTC_ASSERT(!indizes.isEmpty(), return actions);

    bool hasErrors = false;
    for (const QModelIndex &index : indizes) {
        Error error = model()->data(index, ErrorListModel::ErrorRole).value<Error>();
        if (!error.suppression().isNull()) {
            hasErrors = true;
            break;
        }
    }
    m_suppressAction->setEnabled(hasErrors);
    actions << m_suppressAction;
    return actions;
}

static QString suppressionText(const Error &error)
{
    Suppression sup(error.suppression());

    // workaround: https://bugs.kde.org/show_bug.cgi?id=255822
    if (sup.frames().size() >= 24)
        sup.setFrames(sup.frames().mid(0, 23));
    QTC_CHECK(sup.frames().size() < 24);

    // try to set some useful name automatically, instead of "insert_name_here"
    // we take the last stack frame and append the suppression kind, e.g.:
    // QDebug::operator<<(bool) [Memcheck:Cond]
    if (!error.stacks().isEmpty() && !error.stacks().constFirst().frames().isEmpty()) {
        const Frame frame = error.stacks().constFirst().frames().constFirst();

        QString newName;
        if (!frame.functionName().isEmpty())
            newName = frame.functionName();
        else if (!frame.object().isEmpty())
            newName = frame.object();

        if (!newName.isEmpty())
            sup.setName(newName + '[' + sup.kind() + ']');
    }

    return sup.toString();
}

/// @p error input error, which might get hidden when it has the same stack
/// @p suppressed the error that got suppressed already
static bool equalSuppression(const Error &error, const Error &suppressed)
{
    if (error.kind() != suppressed.kind() || error.suppression().isNull())
        return false;

    const SuppressionFrames errorFrames = error.suppression().frames();
    const SuppressionFrames suppressedFrames = suppressed.suppression().frames();

    // limit to 23 frames, see: https://bugs.kde.org/show_bug.cgi?id=255822
    if (qMin(23, suppressedFrames.size()) > errorFrames.size())
        return false;

    int frames = 23;
    if (errorFrames.size() < frames)
        frames = errorFrames.size();

    if (suppressedFrames.size() < frames)
        frames = suppressedFrames.size();

    for (int i = 0; i < frames; ++i)
        if (errorFrames.at(i) != suppressedFrames.at(i))
            return false;

    return true;
}

bool canSaveSuppression(bool fileIsValid, const QString &suppression)
{
    return fileIsValid && !suppression.trimmed().isEmpty();
}

class SuppressionSettings final : public AspectContainer
{
public:
    SuppressionSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Valgrind/SuppressionDialog.qml"));

        file.setQmlName("File");
        file.setLabelText(Tr::tr("Suppression File:"));
        file.setExpectedKind(PathChooserKind::File);
        file.setHistoryCompleter("Valgrind.Suppression.History");
        file.setPromptDialogFilter("*.supp");
        file.setPromptDialogTitle(Tr::tr("Select Suppression File"));

        suppression.setQmlName("Suppression");
        suppression.setLabelText(Tr::tr("Suppression:"));
        suppression.setDisplayStyle(StringAspect::TextEditDisplay);
        // A suppression rule is read line by line against a stack trace.
        suppression.setMonospace(true);
    }

    FilePathAspect file{this};
    StringAspect suppression{this};
};

SuppressionDialog::SuppressionDialog(MemcheckErrorView *view, const QList<Error> &errors)
    : m_view(view)
    , m_cleanupIfCanceled(false)
    , m_errors(errors)
    , m_settings(new SuppressionSettings)
    , m_buttonBox(new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this))
{
    setWindowTitle(Tr::tr("Save Suppression"));

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(m_buttonBox);

    const FilePath defaultSuppFile = view->defaultSuppressionFile();
    if (!defaultSuppFile.exists() && defaultSuppFile.ensureExistingFile())
        m_cleanupIfCanceled = true;
    m_settings->file.setValue(defaultSuppFile.fileName());

    QString suppressions;
    for (const Error &error : std::as_const(m_errors))
        suppressions += suppressionText(error);
    m_settings->suppression.setValue(suppressions);

    // isValid() is written when the drawn field asks, so it is false here and
    // only the signal says when that changes.
    connect(&m_settings->file, &FilePathAspect::validChanged,
            this, &SuppressionDialog::validate);
    connect(&m_settings->suppression, &BaseAspect::changed,
            this, &SuppressionDialog::validate);
    connect(m_buttonBox, &QDialogButtonBox::accepted,
            this, &SuppressionDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected,
            this, &SuppressionDialog::reject);

    validate();
}

SuppressionDialog::~SuppressionDialog() = default;

void SuppressionDialog::accept()
{
    const FilePath path = m_settings->file();
    QTC_ASSERT(!path.isEmpty(), return);
    QTC_ASSERT(!m_settings->suppression().trimmed().isEmpty(), return);

    FileSaver saver(path, QIODevice::Append);
    if (!saver.hasError()) {
        QTextStream stream(saver.file());
        stream << m_settings->suppression();
        saver.setResult(&stream);
    }
    if (const Result<> res = saver.finalize(); !res) {
        FileUtils::showError(res.error());
        return;
    }

    // Add file to project if there is a project containing this file on the file system.
    if (!ProjectExplorer::ProjectManager::projectForFile(path)) {
        for (ProjectExplorer::Project *p : ProjectExplorer::ProjectManager::projects()) {
            if (path.isChildOf(p->projectDirectory())) {
                p->rootProjectNode()->addFiles({path});
                break;
            }
        }
    }

    m_view->settings()->suppressions.addSuppressionFile(path);

    const QModelIndexList indices = Utils::sorted(m_view->selectionModel()->selectedRows(),
                                                  [](const QModelIndex &l, const QModelIndex &r) {
        return l.row() > r.row();
    });
    QAbstractItemModel *model = m_view->model();
    for (const QModelIndex &index : indices) {
        bool removed = model->removeRow(index.row());
        QTC_ASSERT(removed, qt_noop());
        Q_UNUSED(removed)
    }

    // One suppression might hide multiple rows, care for that.
    for (int row = 0; row < model->rowCount(); ++row ) {
        const Error rowError = model->data(
            model->index(row, 0), ErrorListModel::ErrorRole).value<Error>();

        for (const Error &error : std::as_const(m_errors)) {
            if (equalSuppression(rowError, error)) {
                bool removed = model->removeRow(row);
                QTC_CHECK(removed);
                // Gets incremented in the for loop again.
                --row;
                break;
            }
        }
    }

    // Select a new item.
    m_view->setCurrentIndex(indices.first());

    QDialog::accept();
}

void SuppressionDialog::reject()
{
    if (m_cleanupIfCanceled)
        m_view->defaultSuppressionFile().removeFile();

    QDialog::reject();
}

void SuppressionDialog::validate()
{
    m_buttonBox->button(QDialogButtonBox::Save)
        ->setEnabled(canSaveSuppression(m_settings->file.isValid(), m_settings->suppression()));
}

#ifdef WITH_TESTS

class SuppressionDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        SuppressionSettings settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "SuppressionDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhenASuppressionCanBeSaved()
    {
        // Somewhere to write it, and something to write.
        QVERIFY2(!canSaveSuppression(false, "{ ... }"),
                 "a suppression was saved with nowhere to put it");
        QVERIFY2(!canSaveSuppression(true, {}),
                 "an empty suppression was saved");
        QVERIFY(canSaveSuppression(true, "{ ... }"));

        // A rule of only whitespace would suppress nothing, so it is not one.
        QVERIFY2(!canSaveSuppression(true, "  \n\t "),
                 "a suppression of only whitespace was saved");
    }

    void testWhatTheFieldsAccept()
    {
        SuppressionSettings settings;

        // The file is appended to, so it is one that exists.
        QCOMPARE(settings.file.presentation().pathKind, AspectControls::PathKind::File);
        QVERIFY2(settings.file.presentation().promptDialogFilter.contains("supp"),
                 qPrintable(settings.file.presentation().promptDialogFilter));

        // A suppression is read line by line against a stack trace, so its
        // columns have to line up.
        QVERIFY2(settings.suppression.presentation().monospace,
                 "the suppression was not shown in a fixed-pitch font");
        QCOMPARE(settings.suppression.presentation().control, AspectControls::TextEdit);
    }
};

QObject *createSuppressionDialogTest()
{
    return new SuppressionDialogTest;
}

#endif // WITH_TESTS

} // namespace Valgrind::Internal

#ifdef WITH_TESTS
#include "memcheckerrorview.moc"
#endif

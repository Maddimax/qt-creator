// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gerritdialog.h"

#include "gerritmodel.h"
#include "gerritparameters.h"
#include "gerritremotechooser.h"

#include "../gitplugin.h"
#include "../gittr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/fancylineedit.h>
#include <utils/hostosinfo.h>
#include <utils/itemviews.h>
#include <utils/layoutbuilder.h>
#include <utils/progressindicator.h>

#include <QApplication>
#include <QCompleter>
#include <QDesktopServices>
#include <QDialog>
#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStringListModel>
#include <QTextBrowser>
#include <QUrl>

using namespace Utils;

namespace Gerrit::Internal {

static const int maxTitleWidth = 350;

bool canFetchChange(bool fetchRunning, bool hasCurrentChange)
{
    // Two gerrit operations at once mix up, so nothing is offered while one is
    // running.
    return !fetchRunning && hasCurrentChange;
}

QStringList rememberedQueries(const QStringList &queries, const QString &query)
{
    if (query.isEmpty())
        return queries;
    QStringList remembered = queries;
    remembered.removeAll(query);
    remembered.prepend(query);
    return remembered;
}

// The tree of changes. The rows come from GerritModel, which fills itself when
// asked to refresh; the aspect only says which row the reader is on.
class ChangeTreeAspect final : public BaseAspect
{
    Q_OBJECT

public:
    ChangeTreeAspect(AspectContainer *container, QAbstractItemModel *model)
        : BaseAspect(container), m_model(model)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        p.filterPlaceholderText = Git::Tr::tr("Filter");
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

    Q_INVOKABLE void setCurrentIndex(const QModelIndex &index)
    {
        if (m_currentIndex == index)
            return;
        m_currentIndex = index;
        emit currentChanged();
    }

    Q_INVOKABLE void activateIndex(const QModelIndex &index)
    {
        setCurrentIndex(index);
        emit indexActivated();
    }

    QModelIndex currentIndex() const { return m_currentIndex; }

signals:
    void currentChanged();
    void indexActivated();

private:
    QAbstractItemModel *const m_model;
    QPersistentModelIndex m_currentIndex;
};

class GerritDialogSettings final : public AspectContainer
{
public:
    explicit GerritDialogSettings(QAbstractItemModel *model)
        : changes(this, model)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Git/GerritDialog.qml"));

        repository.setQmlName("Repository");

        remote.setQmlName("Remote");
        remote.remote.setLabelText(Git::Tr::tr("Remote:"));
        remote.setFallbackEnabled(true);

        query.setQmlName("Query");
        query.setLabelText(Git::Tr::tr("&Query:"));
        query.setDisplayStyle(StringAspect::LineEditDisplay);
        query.setPlaceHolderText(
            Git::Tr::tr("Change #, hash, tr:id, owner:email or reviewer:email"));

        changes.setQmlName("Changes");

        details.setQmlName("Details");
        details.setTextFormat(AspectControls::TextFormat::RichText);

        // Shown while the list is being fetched, which can take a while.
        fetching.setQmlName("Fetching");
        fetching.setValue(false);

        display.setQmlName("Display");
        display.setActionText(Git::Tr::tr("&Show"));
        cherryPick.setQmlName("CherryPick");
        cherryPick.setActionText(Git::Tr::tr("Cherry &Pick"));
        checkout.setQmlName("Checkout");
        checkout.setActionText(Git::Tr::tr("C&heckout"));
        refresh.setQmlName("Refresh");
        refresh.setActionText(Git::Tr::tr("&Refresh"));
    }

    TextDisplay repository{this};
    GerritRemoteChooserAspect remote{this};
    StringAspect query{this};
    ChangeTreeAspect changes;
    TextDisplay details{this};
    BoolAspect fetching{this};
    ActionAspect display{this};
    ActionAspect cherryPick{this};
    ActionAspect checkout{this};
    ActionAspect refresh{this};
};

GerritDialog::GerritDialog(const std::shared_ptr<GerritServer> &s,
                           const FilePath &repository,
                           QWidget *parent)
    : QDialog(parent)
    , m_server(s)
    , m_model(new GerritModel(this))
    , m_settings(new GerritDialogSettings(m_model))
    , m_buttonBox(new QDialogButtonBox(QDialogButtonBox::Close, this))
{
    setWindowTitle(Git::Tr::tr("Gerrit"));
    resize(950, 706);

    m_settings->query.setCompletions(gerritSettings().savedQueries);
    m_settings->display.setAction([this] { slotFetchDisplay(); });
    m_settings->cherryPick.setAction([this] { slotFetchCherryPick(); });
    m_settings->checkout.setAction([this] { slotFetchCheckout(); });
    m_settings->refresh.setAction([this] { refresh(); });

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(m_buttonBox);

    m_progressIndicatorTimer.setSingleShot(true);
    m_progressIndicatorTimer.setInterval(50); // don't show progress for < 50ms tasks

    connect(&m_settings->changes, &ChangeTreeAspect::currentChanged,
            this, &GerritDialog::slotCurrentChanged);
    connect(&m_settings->changes, &ChangeTreeAspect::indexActivated, this, [this] {
        slotActivated(m_settings->changes.currentIndex());
    });
    connect(&m_settings->remote, &GerritRemoteChooserAspect::remoteChanged,
            this, &GerritDialog::remoteChanged);
    connect(&m_progressIndicatorTimer, &QTimer::timeout,
            this, [this] { setProgressIndicatorVisible(true); });
    connect(m_model, &GerritModel::stateChanged,
            this, &GerritDialog::manageProgressIndicator);
    connect(m_model, &GerritModel::refreshStateChanged, this, [this](bool running) {
        m_settings->refresh.setEnabled(!running);
        slotRefreshStateChanged(running);
    });
    connect(m_model, &GerritModel::errorText, this, [this](const QString &text) {
        if (text.contains("returned error: 401"))
            updateRemotes(true);
    }, Qt::QueuedConnection);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setCurrentPath(repository);
    slotCurrentChanged();
}

FilePath GerritDialog::repositoryPath() const
{
    return m_repository;
}

void GerritDialog::setCurrentPath(const FilePath &path)
{
    if (path == m_repository)
        return;
    m_repository = path;
    m_settings->repository.setText(Git::Internal::msgRepositoryLabel(path));
    updateRemotes();
}

void GerritDialog::updateCompletions(const QString &query)
{
    if (query.isEmpty())
        return;
    gerritSettings().savedQueries = rememberedQueries(gerritSettings().savedQueries, query);
    m_settings->query.setCompletions(gerritSettings().savedQueries);
    gerritSettings().saveQueries();
}

GerritDialog::~GerritDialog() = default;

void GerritDialog::slotActivated(const QModelIndex &i)
{
    if (i.isValid())
        QDesktopServices::openUrl(QUrl(m_model->change(i)->url));
}

void GerritDialog::slotRefreshStateChanged(bool v)
{
    // The tree opens itself out once the changes have arrived; the Qt Quick
    // tree sizes its own columns, which the widget view had to be told to do.
    if (!v && m_model->rowCount())
        emit expandChanges();
}

void GerritDialog::slotFetchDisplay()
{
    const QModelIndex index = currentIndex();
    if (index.isValid())
        emit fetchDisplay(m_model->change(index));
}

void GerritDialog::slotFetchCherryPick()
{
    const QModelIndex index = currentIndex();
    if (index.isValid())
        emit fetchCherryPick(m_model->change(index));
}

void GerritDialog::slotFetchCheckout()
{
    const QModelIndex index = currentIndex();
    if (index.isValid())
        emit fetchCheckout(m_model->change(index));
}

void GerritDialog::refresh()
{
    const QString query = m_settings->query().trimmed();
    updateCompletions(query);
    m_model->refresh(m_server, query);
}

void GerritDialog::scheduleUpdateRemotes()
{
    if (isVisible())
        updateRemotes();
    else
        m_shouldUpdateRemotes = true;
}

void GerritDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_shouldUpdateRemotes) {
        m_shouldUpdateRemotes = false;
        updateRemotes();
    }
}

void GerritDialog::remoteChanged()
{
    const GerritServer server = m_settings->remote.currentServer();
    if (std::shared_ptr<GerritServer> modelServer = m_model->server()) {
        if (*modelServer == server)
           return;
    }
    *m_server = server;
    if (isVisible())
        refresh();
}

void GerritDialog::updateRemotes(bool forceReload)
{
    m_settings->remote.setRepository(m_repository);
    if (m_repository.isEmpty() || !m_repository.isDir())
        return;
    *m_server = gerritSettings().server;
    m_settings->remote.updateRemotes(forceReload);
}

void GerritDialog::manageProgressIndicator()
{
    if (m_model->state() == GerritModel::Running) {
        m_progressIndicatorTimer.start();
    } else {
        m_progressIndicatorTimer.stop();
        setProgressIndicatorVisible(false);
    }
}

QModelIndex GerritDialog::currentIndex() const
{
    return m_settings->changes.currentIndex();
}

void GerritDialog::updateButtons()
{
    const bool enabled = canFetchChange(m_fetchRunning, currentIndex().isValid());
    m_settings->display.setEnabled(enabled);
    m_settings->cherryPick.setEnabled(enabled);
    m_settings->checkout.setEnabled(enabled);
}

void GerritDialog::slotCurrentChanged()
{
    const QModelIndex current = currentIndex();
    m_settings->details.setText(current.isValid() ? m_model->toHtml(current) : QString());
    updateButtons();
}

void GerritDialog::fetchStarted(const QString &changeTitle)
{
    // Disable buttons to prevent parallel gerrit operations which can cause mix-ups.
    m_fetchRunning = true;
    updateButtons();
    const QString toolTip = Git::Tr::tr("Fetching \"%1\"...").arg(changeTitle);
    m_settings->display.setToolTip(toolTip);
    m_settings->cherryPick.setToolTip(toolTip);
    m_settings->checkout.setToolTip(toolTip);
}

void GerritDialog::fetchFinished()
{
    m_fetchRunning = false;
    updateButtons();
    m_settings->display.setToolTip(QString());
    m_settings->cherryPick.setToolTip(QString());
    m_settings->checkout.setToolTip(QString());
}

void GerritDialog::setProgressIndicatorVisible(bool v)
{
    m_settings->fetching.setValue(v);
}

#ifdef WITH_TESTS

class GerritDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        GerritModel model;
        GerritDialogSettings settings(&model);
        const Result<> rendered = Core::aspectFormRenders(&settings, "GerritDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhenAChangeCanBeFetched()
    {
        // Nothing is offered without a change to act on.
        QVERIFY2(!canFetchChange(false, false), "a change could be fetched without choosing one");
        QVERIFY(canFetchChange(false, true));

        // And nothing while one is already being fetched: two gerrit
        // operations at once mix up.
        QVERIFY2(!canFetchChange(true, true),
                 "a second fetch could be started while one was running");
        QVERIFY(!canFetchChange(true, false));
    }

    void testWhichQueriesAreOfferedBack()
    {
        // Most recent first, and nothing remembered twice.
        QCOMPARE(rememberedQueries({}, "owner:me"), (QStringList{"owner:me"}));
        QCOMPARE(rememberedQueries({"a", "b"}, "c"), (QStringList{"c", "a", "b"}));
        QCOMPARE(rememberedQueries({"a", "b"}, "b"), (QStringList{"b", "a"}));

        // An empty query is not a query.
        QCOMPARE(rememberedQueries({"a"}, {}), (QStringList{"a"}));
    }

    void testTheButtonsFollowTheChosenChange()
    {
        GerritModel model;
        GerritDialogSettings settings(&model);

        // Whatever the dialog does with them, the aspects start disabled -
        // nothing is chosen when it opens.
        settings.display.setEnabled(canFetchChange(false, false));
        QVERIFY(!settings.display.isEnabled());
        settings.display.setEnabled(canFetchChange(false, true));
        QVERIFY(settings.display.isEnabled());
    }

    void testTheDrawnTreeHandsBackTheChosenChange()
    {
        // Two lines in the .qml carry the dialog: which change is being looked
        // at, and which one was meant.
        GerritModel model;
        GerritDialogSettings settings(&model);

        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject *const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the dialog was drawn from");

        QObject *tree = nullptr;
        QTRY_VERIFY(tree = root->findChild<QObject *>("changeTree"));

        // Activating a row in the drawn tree reaches the aspect, which is
        // what opens the change. A C++ test calling activateIndex() itself
        // would not notice if that line went from the .qml.
        QSignalSpy activated(&settings.changes, &ChangeTreeAspect::indexActivated);
        QVERIFY(QMetaObject::invokeMethod(tree, "rowActivated",
                                          Q_ARG(QVariant, QVariant::fromValue(QModelIndex()))));
        QTRY_COMPARE(activated.count(), 1);
    }
};

QObject *createGerritDialogTest()
{
    return new GerritDialogTest;
}

#endif // WITH_TESTS

} // Gerrit::Internal

#include "gerritdialog.moc"

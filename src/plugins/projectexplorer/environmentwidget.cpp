// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "environmentwidget.h"

#include "projectexplorertr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/fileutils.h>
#include <coreplugin/find/itemviewfind.h>

#include <utils/filedialogs.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/detailswidget.h>
#include <utils/environment.h>
#include <utils/environmentdialog.h>
#include <utils/environmentmodel.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/itemviews.h>
#include <utils/macroexpander.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/tooltip/tooltip.h>
#include <utils/utilstr.h>
#include <utils/widgets.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QVBoxLayout>

using namespace Utils;

namespace ProjectExplorer {

// A PATH-like variable, as a list rather than as one string. The platform's
// separator is what holds it together on the way in and out.
QStringList pathsFromString(const QString &paths)
{
    return paths.split(Utils::HostOsInfo::pathListSeparator(), Qt::SkipEmptyParts);
}

QString pathsToString(const QStringList &paths)
{
    return paths.join(Utils::HostOsInfo::pathListSeparator());
}

class PathListSettings final : public AspectContainer
{
public:
    PathListSettings(const QString &varName, const QString &paths)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/PathListDialog.qml"));

        // The variable's name is what the widget tree put in its one header.
        entries.setQmlName("Paths");
        entries.setLabelText(varName);
        entries.setDisplayStyle(StringListAspect::DisplayStyle::ListView);
        entries.setUiPathKind(AspectControls::PathKind::ExistingDirectory);
        entries.setUiAllowReordering(true);
        entries.setValue(pathsFromString(paths));
    }

    StringListAspect entries{this};
};

class PathListDialog : public QDialog
{
public:
    PathListDialog(const QString &varName, const QString &paths, QWidget *parent)
        : QDialog(parent)
        , m_settings(varName, paths)
    {
        resize(800, 600);

        const auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                                    | QDialogButtonBox::Cancel);
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

        const auto mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(Core::createAspectForm(&m_settings));
        mainLayout->addWidget(buttonBox);
    }

    QString paths() const { return pathsToString(m_settings.entries()); }

private:
    PathListSettings m_settings;
};

#ifdef WITH_TESTS

namespace Internal {

class PathListDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        PathListSettings settings("PATH", {});
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "PathListDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testAPathListIsOneStringAndSeveralEntries()
    {
        const QString sep = QString(Utils::HostOsInfo::pathListSeparator());
        QCOMPARE(pathsFromString("/one" + sep + "/two"), (QStringList{"/one", "/two"}));
        QCOMPARE(pathsToString({"/one", "/two"}), "/one" + sep + "/two");

        // Empty stretches are not entries: a variable that ends with the
        // separator, or has two in a row, has no nameless directory in it.
        QCOMPARE(pathsFromString("/one" + sep), QStringList{"/one"});
        QCOMPARE(pathsFromString(sep + sep), QStringList());
        QCOMPARE(pathsFromString(""), QStringList());
    }

    void testTheEntriesArePickedAndOrdered()
    {
        // Directories, so Add... and Edit... open a chooser rather than
        // leaving a path to be typed; and the order is the reader's, because
        // a PATH is searched in order.
        PathListSettings settings("PATH", "/one");
        QCOMPARE(settings.entries.presentation().pathKind,
                 Utils::AspectControls::PathKind::ExistingDirectory);
        QVERIFY2(settings.entries.presentation().allowReordering,
                 "the directories can no longer be put in the order they are searched");
    }

    void testWhatTheDialogWasOpenedOnComesBack()
    {
        const QString sep = QString(Utils::HostOsInfo::pathListSeparator());
        PathListDialog dialog("PATH", "/one" + sep + "/two", nullptr);
        QCOMPARE(dialog.paths(), "/one" + sep + "/two");
    }

    void testTheVariableNamesItself()
    {
        // The widget tree put the variable's name in its one header; there is
        // no header here, so it is the list's label.
        PathListSettings settings("LD_LIBRARY_PATH", {});
        QCOMPARE(settings.entries.labelText(), QString("LD_LIBRARY_PATH"));
    }
};

QObject *createPathListDialogTest()
{
    return new PathListDialogTest;
}

} // namespace Internal

#endif // WITH_TESTS

////
// EnvironmentWidget::EnvironmentWidget
////

class EnvironmentWidget::Private
{
public:
    Private(EnvironmentWidget *q) : q(q) {}

    void handleEditRequest(NameValueItemsWidget::Selection selection);

    EnvironmentWidget * const q;
    Utils::NameValueItemsWidget m_editor;
    Utils::EnvironmentModel *m_model = nullptr;
    EnvironmentWidget::Type m_type = EnvironmentWidget::TypeLocal;
    QString m_baseEnvironmentText;
    EnvironmentWidget::OpenTerminalFunc m_openTerminalFunc;
    Utils::DetailsWidget *m_detailsContainer = nullptr;
    QTreeView *m_environmentView = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_unsetButton = nullptr;
    QPushButton *m_toggleButton = nullptr;
    QPushButton *m_appendPathButton = nullptr;
    QPushButton *m_prependPathButton = nullptr;
    QPushButton *m_terminalButton = nullptr;
};

EnvironmentWidget::EnvironmentWidget(QWidget *parent, Type type, QWidget *additionalDetailsWidget)
    : QWidget(parent), d(std::make_unique<Private>(this))
{
    d->m_model = new Utils::EnvironmentModel();
    d->m_type = type;
    connect(d->m_model, &Utils::EnvironmentModel::userChangesChanged,
            this, &EnvironmentWidget::userChangesChanged);
    connect(d->m_model, &QAbstractItemModel::modelReset,
            this, &EnvironmentWidget::invalidateCurrentIndex);
    connect(d->m_model, &Utils::EnvironmentModel::focusIndex,
            this, &EnvironmentWidget::focusIndex);

    // The text edit represents the raw, unexpanded user changes.
    // There are two possible data flows:
    //   a) text edit -> model -> view for when the user types in the text edit
    //   b) top level widget
    //        -> text edit
    //        -> model -> view
    //      for when convenience functionality is used via the buttons.
    //   So the model is always updated from the text edit or text edit and model
    //   are updated in sync from this widget.
    connect(&d->m_editor, &NameValueItemsWidget::userChangedItems,
            d->m_model, &EnvironmentModel::setUserChanges);

    auto vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);

    d->m_detailsContainer = new Utils::DetailsWidget(this);
    connect(d->m_detailsContainer, &DetailsWidget::expanded,
            this, &EnvironmentWidget::updateSummaryText);

    auto details = new QWidget(d->m_detailsContainer);
    d->m_detailsContainer->setWidget(details);
    details->setVisible(false);

    auto vbox2 = new QVBoxLayout(details);
    vbox2->setContentsMargins(0, 0, 0, 0);

    if (additionalDetailsWidget)
        vbox2->addWidget(additionalDetailsWidget);

    auto horizontalLayout = new QHBoxLayout();
    horizontalLayout->setContentsMargins(0, 0, 0, 0);
    d->m_environmentView = new Utils::TreeView(this);
    d->m_environmentView->setModel(d->m_model);
    d->m_environmentView->setMinimumHeight(400);
    d->m_environmentView->setRootIsDecorated(false);
    const auto stretcher = new HeaderViewStretcher(d->m_environmentView->header(), 1);
    connect(d->m_model, &QAbstractItemModel::dataChanged,
            stretcher, &HeaderViewStretcher::softStretch);
    connect(d->m_model, &EnvironmentModel::userChangesChanged,
            stretcher, &HeaderViewStretcher::softStretch);
    d->m_environmentView->setSelectionMode(QAbstractItemView::SingleSelection);
    d->m_environmentView->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->m_environmentView->setFrameShape(QFrame::NoFrame);
    QFrame *findWrapper = Core::ItemViewFind::createSearchableWrapper(d->m_environmentView, Core::ItemViewFind::LightColored);
    findWrapper->setFrameStyle(QFrame::StyledPanel);
    horizontalLayout->addWidget(findWrapper);

    auto buttonLayout = new QVBoxLayout();

    d->m_editButton = new QPushButton(this);
    d->m_editButton->setText(Tr::tr("Ed&it"));
    buttonLayout->addWidget(d->m_editButton);

    d->m_addButton = new QPushButton(this);
    d->m_addButton->setText(Tr::tr("&Add"));
    buttonLayout->addWidget(d->m_addButton);

    d->m_resetButton = new QPushButton(this);
    d->m_resetButton->setEnabled(false);
    d->m_resetButton->setText(Tr::tr("&Reset"));
    buttonLayout->addWidget(d->m_resetButton);

    d->m_unsetButton = new QPushButton(this);
    d->m_unsetButton->setEnabled(false);
    d->m_unsetButton->setText(Tr::tr("&Unset"));
    buttonLayout->addWidget(d->m_unsetButton);

    d->m_toggleButton = new QPushButton(Tr::tr("Disable"), this);
    buttonLayout->addWidget(d->m_toggleButton);
    connect(d->m_toggleButton, &QPushButton::clicked, this, [this] {
        d->m_model->toggleVariable(d->m_environmentView->currentIndex());
        d->m_editor.setEnvironmentChanges(d->m_model->changes());
        updateButtons();
    });

    if (type == TypeLocal) {
        d->m_appendPathButton = new QPushButton(this);
        d->m_appendPathButton->setEnabled(false);
        d->m_appendPathButton->setText(Tr::tr("Append Path..."));
        buttonLayout->addWidget(d->m_appendPathButton);
        d->m_prependPathButton = new QPushButton(this);
        d->m_prependPathButton->setEnabled(false);
        d->m_prependPathButton->setText(Tr::tr("Prepend Path..."));
        buttonLayout->addWidget(d->m_prependPathButton);
        connect(d->m_appendPathButton, &QAbstractButton::clicked,
                this, &EnvironmentWidget::appendPathButtonClicked);
        connect(d->m_prependPathButton, &QAbstractButton::clicked,
                this, &EnvironmentWidget::prependPathButtonClicked);
    }

    d->m_terminalButton = new QPushButton(this);
    d->m_terminalButton->setText(Tr::tr("Open &Terminal"));
    d->m_terminalButton->setToolTip(Tr::tr("Open a terminal with this environment set up."));
    d->m_terminalButton->setEnabled(true);
    buttonLayout->addWidget(d->m_terminalButton);
    buttonLayout->addStretch();

    horizontalLayout->addLayout(buttonLayout);
    horizontalLayout->addWidget(&d->m_editor);
    vbox2->addLayout(horizontalLayout);

    vbox->addWidget(d->m_detailsContainer);

    updateButtons();
    connect(d->m_model, &QAbstractItemModel::dataChanged,
            this, &EnvironmentWidget::updateButtons);

    connect(d->m_editButton, &QAbstractButton::clicked,
            this, &EnvironmentWidget::editEnvironmentButtonClicked);
    connect(d->m_addButton, &QAbstractButton::clicked,
            this, &EnvironmentWidget::addEnvironmentButtonClicked);
    connect(d->m_resetButton, &QAbstractButton::clicked,
            this, &EnvironmentWidget::removeEnvironmentButtonClicked);
    connect(d->m_unsetButton, &QAbstractButton::clicked,
            this, &EnvironmentWidget::unsetEnvironmentButtonClicked);
    connect(d->m_environmentView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &EnvironmentWidget::environmentCurrentIndexChanged);
    connect(d->m_terminalButton, &QAbstractButton::clicked,
            this, [this] {
        Utils::Environment env = d->m_model->baseEnvironment();
        d->m_model->changes().modifyEnvironment(env, globalMacroExpander()); // FIXME: Give this class the right expander for the context.
        if (d->m_openTerminalFunc)
            d->m_openTerminalFunc(env);
        else
            Core::FileUtils::openTerminal(FilePath::fromString(QDir::currentPath()), env);
    });
    connect(d->m_detailsContainer, &Utils::DetailsWidget::linkActivated,
            this, &EnvironmentWidget::linkActivated);

    connect(d->m_model, &Utils::EnvironmentModel::userChangesChanged,
            this, &EnvironmentWidget::updateSummaryText);
}

EnvironmentWidget::~EnvironmentWidget()
{
    delete d->m_model;
    d->m_model = nullptr;
}

void EnvironmentWidget::focusIndex(const QModelIndex &index)
{
    d->m_environmentView->setCurrentIndex(index);
    d->m_environmentView->setFocus();
    // When the current item changes as a result of the call above,
    // QAbstractItemView::currentChanged() is called. That calls scrollTo(current),
    // using the default EnsureVisible scroll hint, whereas we want PositionAtTop,
    // because it ensures that the user doesn't have to scroll down when they've
    // added a new environment variable and want to edit its value; they'll be able
    // to see its value as they're typing it.
    // This only helps to a certain degree - variables whose names start with letters
    // later in the alphabet cause them fall within the "end" of the view's range,
    // making it impossible to position them at the top of the view.
    d->m_environmentView->scrollTo(index, QAbstractItemView::PositionAtTop);
}

void EnvironmentWidget::setBaseEnvironment(const Utils::Environment &env)
{
    d->m_model->setBaseEnvironment(env);
}

void EnvironmentWidget::setBaseEnvironmentText(const QString &text)
{
    d->m_baseEnvironmentText = text;
    updateSummaryText();
}

Utils::EnvironmentChanges EnvironmentWidget::changes() const
{
    forceUpdateCheck();
    return d->m_model->changes();
}

void EnvironmentWidget::setChanges(const Utils::EnvironmentChanges &changes)
{
    d->m_model->setUserChanges(changes);
    d->m_editor.setEnvironmentChanges(changes);
}

void EnvironmentWidget::setBrowseHint(const Utils::FilePath &browseHint)
{
    d->m_editor.setBrowseHint(browseHint);
}

void EnvironmentWidget::setOpenTerminalFunc(const EnvironmentWidget::OpenTerminalFunc &func)
{
    d->m_openTerminalFunc = func;
    d->m_terminalButton->setVisible(bool(func));
}

void EnvironmentWidget::expand()
{
    d->m_detailsContainer->setState(Utils::DetailsWidget::Expanded);
}

void EnvironmentWidget::forceUpdateCheck() const
{
    d->m_editor.forceUpdateCheck();
}

void EnvironmentWidget::updateSummaryText()
{
    // The summary is redundant with the text edit, so we hide it on expansion.
    if (d->m_detailsContainer->state() == DetailsWidget::Expanded) {
        d->m_detailsContainer->setSummaryText({});
        return;
    }

    EnvironmentItems list = d->m_model->effectiveDiff();
    list = Utils::filtered(list, [](const EnvironmentItem &it) {
        return it.operation != Utils::EnvironmentItem::Comment;
    });
    Utils::EnvironmentItem::sort(&list);

    QString text;
    for (const Utils::EnvironmentItem &item : std::as_const(list)) {
        if (item.name != ::Utils::Tr::tr("<VARIABLE>")) {
            if (!d->m_baseEnvironmentText.isEmpty() || !text.isEmpty())
                text.append(QLatin1String("<br>"));
            switch (item.operation) {
            case Utils::EnvironmentItem::Unset:
                text.append(Tr::tr("Unset <a href=\"%1\"><b>%1</b></a>").arg(item.name.toHtmlEscaped()));
                break;
            case Utils::EnvironmentItem::SetEnabled:
                text.append(Tr::tr("Set <a href=\"%1\"><b>%1</b></a> to <b>%2</b>").arg(item.name.toHtmlEscaped(), item.value.toHtmlEscaped()));
                break;
            case Utils::EnvironmentItem::Append:
                text.append(Tr::tr("Append <b>%2</b> to <a href=\"%1\"><b>%1</b></a>").arg(item.name.toHtmlEscaped(), item.value.toHtmlEscaped()));
                break;
            case Utils::EnvironmentItem::Prepend:
                text.append(Tr::tr("Prepend <b>%2</b> to <a href=\"%1\"><b>%1</b></a>").arg(item.name.toHtmlEscaped(), item.value.toHtmlEscaped()));
                break;
            case Utils::EnvironmentItem::SetDisabled:
                text.append(Tr::tr("Set <a href=\"%1\"><b>%1</b></a> to <b>%2</b> [disabled]").arg(item.name.toHtmlEscaped(), item.value.toHtmlEscaped()));
                break;
            case Utils::EnvironmentItem::Comment:
                break;
            }
        }
    }

    if (text.isEmpty()) {
        //: %1 is "System Environment" or some such.
        if (!d->m_baseEnvironmentText.isEmpty())
            text.prepend(Tr::tr("Use %1").arg("<b>" + d->m_baseEnvironmentText + "</b>"));
        else
            text.prepend("<b>" + Tr::tr("No environment changes") + "</b>");
    } else {
        //: Yup, word puzzle. The Set/Unset phrases above are appended to this.
        //: %1 is "System Environment" or some such.
        if (!d->m_baseEnvironmentText.isEmpty())
            text.prepend(Tr::tr("Use %1 and").arg("<b>" + d->m_baseEnvironmentText + "</b>"));
    }

    d->m_detailsContainer->setSummaryText(text);
}

void EnvironmentWidget::linkActivated(const QString &link)
{
    d->m_detailsContainer->setState(Utils::DetailsWidget::Expanded);
    QModelIndex idx = d->m_model->variableToIndex(link);
    focusIndex(idx);
}

void EnvironmentWidget::updateButtons()
{
    environmentCurrentIndexChanged(d->m_environmentView->currentIndex());
}

void EnvironmentWidget::editEnvironmentButtonClicked()
{
    d->handleEditRequest(NameValueItemsWidget::Selection::Value);
}

void EnvironmentWidget::Private::handleEditRequest(NameValueItemsWidget::Selection selection)
{
    QModelIndex current = m_environmentView->currentIndex();
    if (current.column() == 0)
        current = current.siblingAtColumn(1);
    const QString varName = m_model->indexToVariable(current);

    // Known path lists are edited via a dedicated widget.
    if (m_type == TypeLocal && m_model->currentEntryIsPathList(current)) {
        PathListDialog dlg(varName, m_model->data(current).toString(), q);
        if (dlg.exec() == QDialog::Accepted) {
            m_model->setData(current, dlg.paths());
            m_editor.setEnvironmentChanges(m_model->changes());
        }
        return;
    }

    if (m_editor.editVariable(varName, selection))
        return;

    // The variable does not appear on an LHS in the text edit. This means it is not
    // set or unset in any of the user changes. Therefore, we have to create a new
    // change item for the user to edit.
    EnvironmentChanges changes = m_model->changes();
    changes.appendUserItem({varName, "NEWVALUE"});
    q->setChanges(changes);
    const bool editable = m_editor.editVariable(varName, NameValueItemsWidget::Selection::Value);
    QTC_CHECK(editable);
}

void EnvironmentWidget::addEnvironmentButtonClicked()
{
    QModelIndex index = d->m_model->addVariable();
    d->m_editor.setEnvironmentChanges(d->m_model->changes());
    d->m_environmentView->setCurrentIndex(index);
    d->handleEditRequest(NameValueItemsWidget::Selection::Name);
}

void EnvironmentWidget::removeEnvironmentButtonClicked()
{
    const QString &name = d->m_model->indexToVariable(d->m_environmentView->currentIndex());
    d->m_model->resetVariable(name);
    d->m_editor.setEnvironmentChanges(d->m_model->changes());
}

// unset in Merged Environment Mode means, unset if it comes from the base environment
// or remove when it is just a change we added
void EnvironmentWidget::unsetEnvironmentButtonClicked()
{
    const QString &name = d->m_model->indexToVariable(d->m_environmentView->currentIndex());
    if (!d->m_model->canReset(name))
        d->m_model->resetVariable(name);
    else
        d->m_model->unsetVariable(name);
    d->m_editor.setEnvironmentChanges(d->m_model->changes());
}

void EnvironmentWidget::amendPathList(Utils::EnvironmentItem::Operation op)
{
    const QString varName = d->m_model->indexToVariable(d->m_environmentView->currentIndex());
    const FilePath dir = FileUtils::getExistingDirectory(Tr::tr("Choose Directory"));
    if (dir.isEmpty())
        return;
    EnvironmentChanges changes = d->m_model->changes();
    changes.appendUserItem({varName, dir.toUserOutput(), op});
    setChanges(changes);
}

void EnvironmentWidget::appendPathButtonClicked()
{
    amendPathList(Utils::EnvironmentItem::Append);
}

void EnvironmentWidget::prependPathButtonClicked()
{
    amendPathList(Utils::EnvironmentItem::Prepend);
}

void EnvironmentWidget::environmentCurrentIndexChanged(const QModelIndex &current)
{
    if (current.isValid()) {
        d->m_editButton->setEnabled(true);
        const QString &name = d->m_model->indexToVariable(current);
        bool modified = d->m_model->canReset(name) && d->m_model->hasExplicitChanges(name);
        bool unset = d->m_model->isUnset(name);
        d->m_resetButton->setEnabled(modified || unset);
        d->m_unsetButton->setEnabled(!unset);
        d->m_toggleButton->setEnabled(!unset);
        d->m_toggleButton->setText(d->m_model->isEnabled(name) ? Tr::tr("Disable") : Tr::tr("Enable"));
    } else {
        d->m_editButton->setEnabled(false);
        d->m_resetButton->setEnabled(false);
        d->m_unsetButton->setEnabled(false);
        d->m_toggleButton->setEnabled(false);
        d->m_toggleButton->setText(Tr::tr("Disable"));
    }
    if (d->m_appendPathButton) {
        const bool isPathList = d->m_model->currentEntryIsPathList(current);
        d->m_appendPathButton->setEnabled(isPathList);
        d->m_prependPathButton->setEnabled(isPathList);
    }
}

void EnvironmentWidget::invalidateCurrentIndex()
{
    environmentCurrentIndexChanged(QModelIndex());
}

void EnvironmentWidget::setupDirtyHooks() const
{
    d->m_editor.setupDirtyHooks();
}

bool editEnvironmentPathList(Utils::EnvironmentModel *model,
                             const QModelIndex &index,
                             QWidget *parent)
{
    QTC_ASSERT(model, return false);
    if (!index.isValid())
        return false;
    const QModelIndex valueIndex = index.column() == 0 ? index.siblingAtColumn(1) : index;
    PathListDialog dlg(model->indexToVariable(valueIndex),
                       model->data(valueIndex).toString(),
                       parent);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    model->setData(valueIndex, dlg.paths());
    return true;
}

} // namespace ProjectExplorer

#ifdef WITH_TESTS
#include "environmentwidget.moc"
#endif

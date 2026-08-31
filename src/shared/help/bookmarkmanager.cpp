// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bookmarkmanager.h"

#include <localhelpmanager.h>

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <help/helptr.h>

#include <utils/aspects.h>
#include <utils/fancylineedit.h>
#include <utils/guard.h>
#include <utils/layoutbuilder.h>
#include <utils/utilsicons.h>
#include <utils/widgets.h>

#include <QMenu>
#include <QIcon>
#include <QStyle>
#include <QLabel>
#include <QLayout>
#include <QEvent>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QHeaderView>
#include <QToolButton>
#include <QPushButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>
#include <QRegularExpression>

#include <QHelpEngine>

using namespace Help::Internal;

static const char kBookmarksKey[] = "Help/Bookmarks";

namespace {

const char kFolderData[] = "Folder";
const int kKindRole = Qt::UserRole + 10;

// The folders, with the role names a Qt Quick view addresses its cells by and
// one row at a time made writable. A folder is renamed by writing its cell,
// and a cell that is always writable is a text field the reader cannot click
// past - which is what picking a folder in this tree is mostly for.
class BookmarkFolderModel : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setRenaming(const QModelIndex &index)
    {
        const QPersistentModelIndex was = m_renaming;
        m_renaming = index;
        const auto redraw = [this](const QModelIndex &idx) {
            if (idx.isValid())
                emit dataChanged(idx, idx, {Utils::AspectTable::EditableRole});
        };
        redraw(was);
        redraw(m_renaming);
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return Utils::AspectTable::withRoleNames(QSortFilterProxyModel::roleNames());
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Utils::AspectTable::CheckableRole)
            return false;
        if (role == Utils::AspectTable::EditableRole)
            return m_renaming.isValid() && index == QModelIndex(m_renaming);
        return QSortFilterProxyModel::data(index, role);
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        const bool written = QSortFilterProxyModel::setData(index, value, role);
        // The name has been given, so the row has stopped being renamed.
        if (written && role == Qt::EditRole)
            setRenaming({});
        return written;
    }

private:
    QPersistentModelIndex m_renaming;
};

// The tree of folders. Its rows offer what the widget dialog put behind a
// right click, and it is told which row to put the reader on when the box
// above it is used instead.
class BookmarkFolderAspect final : public Utils::BaseAspect
{
public:
    BookmarkFolderAspect(Utils::AspectContainer *container, BookmarkFolderModel *model)
        : BaseAspect(container), m_model(model)
    {}

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = BaseAspect::presentation();
        p.control = Utils::AspectControls::Tree;
        p.rowActions = {{::Help::Tr::tr("Delete Folder"), {}, true, QString("delete")},
                        {::Help::Tr::tr("Rename Folder"), {}, true, QString("rename")}};
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

    void setOnRowAction(const std::function<void(const QModelIndex &, const QString &)> &action)
    {
        m_onRowAction = action;
    }

    void triggerRowAction(const QModelIndex &index, const QVariant &id) override
    {
        if (m_onRowAction)
            m_onRowAction(index, id.toString());
    }

    // Where the reader is, in the proxy's rows. Said by the drawn tree.
    void setCurrentIndex(const QModelIndex &index) override
    {
        if (m_chosen == index)
            return;
        m_chosen = index;
        emit changed();
    }

    QModelIndex chosenIndex() const { return m_chosen; }

private:
    BookmarkFolderModel *const m_model;
    std::function<void(const QModelIndex &, const QString &)> m_onRowAction;
    QPersistentModelIndex m_chosen;
};

} // namespace

// What the dialog asks: what to call the bookmark, and which folder to put it
// in - named in the box, or picked in the tree the box opens.
class BookmarkDialogAspects final : public Utils::AspectContainer
{
public:
    BookmarkDialogAspects(BookmarkManager *manager, const QString &title);

    // Where the bookmark goes, in the manager's own model. Invalid means the
    // top, which is what "Bookmarks" stands for.
    QModelIndex chosenFolder() const
    {
        return m_folderModel.mapToSource(folders.chosenIndex());
    }

    void setDialogParent(QWidget *parent) { m_dialogParent = parent; }

    Utils::StringAspect name{this};
    Utils::StringSelectionAspect folder{this};
    Utils::ActionAspect showFolders{this};
    BookmarkFolderAspect folders;
    Utils::ActionAspect newFolder{this};

private:
    void showFolderNames();
    void showChosenFolderInTree();
    void nameTheChosenFolder();
    void addFolder();
    void actOnRow(const QModelIndex &index, const QString &id);

    BookmarkManager *const m_manager;
    BookmarkFolderModel m_folderModel;
    QStringList m_folderNames;
    QPointer<QWidget> m_dialogParent;
    Utils::Guard m_choosing;
};

BookmarkDialogAspects::BookmarkDialogAspects(BookmarkManager *manager, const QString &title)
    : folders(this, &m_folderModel)
    , m_manager(manager)
{
    setAutoApply(true);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Help/BookmarkDialog.qml"));

    m_folderModel.setFilterKeyColumn(0);
    m_folderModel.setDynamicSortFilter(true);
    m_folderModel.setFilterRole(kKindRole);
    m_folderModel.setSourceModel(manager->treeBookmarkModel());
    m_folderModel.setFilterRegularExpression(QRegularExpression(QLatin1String(kFolderData)));

    name.setQmlName("Name");
    name.setLabelText(::Help::Tr::tr("Bookmark:"));
    name.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    name.setValue(title);

    folder.setQmlName("Folder");
    folder.setLabelText(::Help::Tr::tr("Add in folder:"));
    // A folder is picked from those that exist, never typed.
    folder.setComboBoxEditable(false);
    folder.setFillCallback([this](const Utils::StringSelectionAspect::ResultCallback &cb) {
        QList<QStandardItem *> items;
        for (const QString &folderName : std::as_const(m_folderNames)) {
            auto item = new QStandardItem(folderName);
            item->setData(folderName);
            items.append(item);
        }
        cb(items);
    });

    showFolders.setQmlName("ShowFolders");
    showFolders.setActionText(QLatin1String("+"));
    showFolders.setToolTip(::Help::Tr::tr("Show the folders"));
    showFolders.setAction([this] {
        const bool wasShown = folders.isVisible();
        showFolders.setActionText(wasShown ? QLatin1String("+") : QLatin1String("-"));
        folders.setVisible(!wasShown);
        newFolder.setVisible(!wasShown);
    });

    folders.setQmlName("Folders");
    folders.setVisible(false);
    folders.setOnRowAction([this](const QModelIndex &index, const QString &id) {
        actOnRow(index, id);
    });

    newFolder.setQmlName("NewFolder");
    newFolder.setActionText(::Help::Tr::tr("New Folder"));
    newFolder.setVisible(false);
    newFolder.setAction([this] { addFolder(); });

    // The box and the tree are two ways of saying the same thing.
    connect(&folder, &Utils::BaseAspect::changed, this, [this] {
        if (!m_choosing.isLocked())
            showChosenFolderInTree();
    });
    connect(&folders, &Utils::BaseAspect::changed, this, [this] {
        if (!m_choosing.isLocked())
            nameTheChosenFolder();
    });
    // Renaming or removing a folder changes what the box has to offer.
    connect(manager->treeBookmarkModel(), &QStandardItemModel::dataChanged,
            this, [this] { showFolderNames(); });

    showFolderNames();
}

void BookmarkDialogAspects::showFolderNames()
{
    const QString chosen = folder.value();
    m_folderNames = m_manager->bookmarkFolders();
    folder.ensureFilled();
    folder.refill();
    if (m_folderNames.contains(chosen)) {
        const Utils::GuardLocker locker(m_choosing);
        folder.setValue(chosen);
    }
}

void BookmarkDialogAspects::showChosenFolderInTree()
{
    const Utils::GuardLocker locker(m_choosing);
    const QString wanted = folder.value();
    if (wanted == ::Help::Tr::tr("Bookmarks")) {
        folders.setCurrentIndex({});
        folders.showIndexInControl({});
        return;
    }
    const QList<QStandardItem *> found = m_manager->treeBookmarkModel()->findItems(
        wanted, Qt::MatchCaseSensitive | Qt::MatchRecursive, 0);
    if (found.isEmpty())
        return;
    const QModelIndex index = m_folderModel.mapFromSource(
        m_manager->treeBookmarkModel()->indexFromItem(found.first()));
    folders.setCurrentIndex(index);
    folders.showIndexInControl(index);
}

void BookmarkDialogAspects::nameTheChosenFolder()
{
    const Utils::GuardLocker locker(m_choosing);
    const QModelIndex index = folders.chosenIndex();
    folder.setValue(index.isValid() ? index.data().toString() : ::Help::Tr::tr("Bookmarks"));
}

void BookmarkDialogAspects::addFolder()
{
    const QModelIndex added = m_manager->addNewFolder(chosenFolder());
    if (!added.isValid())
        return;
    showFolderNames();
    const QModelIndex shown = m_folderModel.mapFromSource(added);
    folders.setCurrentIndex(shown);
    folders.showIndexInControl(shown);
    nameTheChosenFolder();
}

void BookmarkDialogAspects::actOnRow(const QModelIndex &index, const QString &id)
{
    if (!index.isValid())
        return;
    if (id == QLatin1String("rename")) {
        m_folderModel.setRenaming(index);
        return;
    }
    if (id == QLatin1String("delete")) {
        m_manager->removeBookmarkItem(m_dialogParent, m_folderModel.mapToSource(index));
        folders.setCurrentIndex({});
        showFolderNames();
        nameTheChosenFolder();
    }
}

BookmarkDialog::BookmarkDialog(BookmarkManager *manager, const QString &title,
        const QString &url, QWidget *parent)
    : QDialog(parent)
    , m_url(url)
    , bookmarkManager(manager)
    , m_aspects(new BookmarkDialogAspects(manager, title))
{
    resize(450, 0);
    setWindowTitle(::Help::Tr::tr("Add Bookmark"));
    m_aspects->setDialogParent(this);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &BookmarkDialog::reject);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &BookmarkDialog::addAccepted);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_aspects.get()));
    layout->addWidget(m_buttonBox);

    // A bookmark with no name is not one that can be found again.
    const auto updateOk = [this] {
        m_buttonBox->button(QDialogButtonBox::Ok)
            ->setEnabled(!m_aspects->name.value().trimmed().isEmpty());
    };
    connect(&m_aspects->name, &Utils::BaseAspect::changed, this, updateOk);
    updateOk();
}

BookmarkDialog::~BookmarkDialog() = default;

void BookmarkDialog::addAccepted()
{
    bookmarkManager->addNewBookmark(m_aspects->chosenFolder(), m_aspects->name.value(), m_url);
    accept();
}


// #pragma mark -- BookmarkWidget


BookmarkWidget::BookmarkWidget(BookmarkManager *manager, QWidget *parent)
    : QWidget(parent)
    , bookmarkManager(manager)
    , m_isOpenInNewPageActionVisible(true)
{
    setup();
    installEventFilter(this);
}

BookmarkWidget::~BookmarkWidget()
{
}

void BookmarkWidget::setOpenInNewPageActionVisible(bool visible)
{
    m_isOpenInNewPageActionVisible = visible;
}

void BookmarkWidget::filterChanged()
{
    bool searchBookmarks = searchField->text().isEmpty();
    if (!searchBookmarks) {
        regExp.setPattern(QRegularExpression::escape(searchField->text()));
        filterBookmarkModel->setSourceModel(bookmarkManager->listBookmarkModel());
    } else {
        regExp.setPattern(QString());
        filterBookmarkModel->setSourceModel(bookmarkManager->treeBookmarkModel());
    }

    filterBookmarkModel->setFilterRegularExpression(regExp);

    const QModelIndex &index = treeView->indexAt(QPoint(1, 1));
    if (index.isValid())
        treeView->setCurrentIndex(index);

    if (searchBookmarks)
        expandItems();
}

void BookmarkWidget::expand(const QModelIndex& index)
{
    const QModelIndex& source = filterBookmarkModel->mapToSource(index);
    QStandardItem *item =
        bookmarkManager->treeBookmarkModel()->itemFromIndex(source);
    if (item)
        item->setData(treeView->isExpanded(index), Qt::UserRole + 11);
}

void BookmarkWidget::activated(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    QString data = index.data(Qt::UserRole + 10).toString();
    if (data != QLatin1String("Folder"))
        emit linkActivated(data);
}

void BookmarkWidget::showContextMenu(const QPoint &point)
{
    QModelIndex index = treeView->indexAt(point);
    if (!index.isValid())
        return;

    QAction *showItem = 0;
    QAction *removeItem = 0;
    QAction *renameItem = 0;
    QAction *showItemNewTab = 0;

    QMenu menu(this);
    QString data = index.data(Qt::UserRole + 10).toString();
    if (data == QLatin1String("Folder")) {
        removeItem = menu.addAction(::Help::Tr::tr("Delete Folder"));
        renameItem = menu.addAction(::Help::Tr::tr("Rename Folder"));
    } else {
        showItem = menu.addAction(::Help::Tr::tr("Show Bookmark"));
        if (m_isOpenInNewPageActionVisible)
            showItemNewTab = menu.addAction(::Help::Tr::tr("Show Bookmark as New Page"));
        if (searchField->text().isEmpty()) {
            menu.addSeparator();
            removeItem = menu.addAction(::Help::Tr::tr("Delete Bookmark"));
            renameItem = menu.addAction(::Help::Tr::tr("Rename Bookmark"));
        }
    }

    QAction *pickedAction = menu.exec(treeView->mapToGlobal(point));
    if (!pickedAction)
        return;

    if (pickedAction == showItem) {
        emit linkActivated(data);
    } else if (pickedAction == showItemNewTab) {
        emit createPage(QUrl(data), false);
    } else if (pickedAction == removeItem) {
        bookmarkManager->removeBookmarkItem(treeView,
            filterBookmarkModel->mapToSource(index));
    } else if (pickedAction == renameItem) {
        const QModelIndex &source = filterBookmarkModel->mapToSource(index);
        QStandardItem *item =
            bookmarkManager->treeBookmarkModel()->itemFromIndex(source);
        if (item) {
            item->setEditable(true);
            treeView->edit(index);
            item->setEditable(false);
        }
    }
}

void BookmarkWidget::setup()
{
    regExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);

    QLayout *vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0, 0, 0, 0);
    vlayout->setSpacing(0);

    searchField = new Utils::FancyLineEdit(this);
    searchField->setFiltering(true);
    setFocusProxy(searchField);

    Utils::StyledBar *toolbar = new Utils::StyledBar(this);
    toolbar->setSingleRow(false);
    QLayout *tbLayout = new QHBoxLayout();
    tbLayout->setContentsMargins(4, 4, 4, 4);
    tbLayout->addWidget(searchField);
    toolbar->setLayout(tbLayout);

    vlayout->addWidget(toolbar);

    searchField->installEventFilter(this);
    connect(searchField, &Utils::FancyLineEdit::textChanged,
            this, &BookmarkWidget::filterChanged);

    treeView = new TreeView(this);
    vlayout->addWidget(treeView);

    filterBookmarkModel = new QSortFilterProxyModel(this);
    treeView->setModel(filterBookmarkModel);

    treeView->setDragEnabled(true);
    treeView->setAcceptDrops(true);
    treeView->setAutoExpandDelay(1000);
    treeView->setDropIndicatorShown(true);
    treeView->viewport()->installEventFilter(this);
    treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    // work around crash on Windows with drag & drop
    // in combination with proxy model and ResizeToContents section resize mode
    treeView->header()->setSectionResizeMode(QHeaderView::Stretch);


    connect(treeView, &TreeView::expanded, this, &BookmarkWidget::expand);
    connect(treeView, &TreeView::collapsed, this, &BookmarkWidget::expand);
    connect(treeView, &TreeView::activated, this, &BookmarkWidget::activated);
    connect(treeView, &TreeView::customContextMenuRequested,
            this, &BookmarkWidget::showContextMenu);

    filterBookmarkModel->setFilterKeyColumn(0);
    filterBookmarkModel->setDynamicSortFilter(true);
    filterBookmarkModel->setSourceModel(bookmarkManager->treeBookmarkModel());

    expandItems();
}

void BookmarkWidget::expandItems()
{
    QStandardItemModel *model = bookmarkManager->treeBookmarkModel();
    const QList<QStandardItem *> list = model->findItems(QLatin1String("*"),
                                                         Qt::MatchWildcard | Qt::MatchRecursive,
                                                         0);
    for (const QStandardItem *item : list) {
        const QModelIndex& index = model->indexFromItem(item);
        treeView->setExpanded(filterBookmarkModel->mapFromSource(index),
            item->data(Qt::UserRole + 11).toBool());
    }
}

bool BookmarkWidget::eventFilter(QObject *object, QEvent *e)
{
    if ((object == this) || (object == treeView->viewport())) {
        QModelIndex index = treeView->currentIndex();
        if (e->type() == QEvent::KeyPress) {
            QKeyEvent *ke = static_cast<QKeyEvent*>(e);
            if (index.isValid() && searchField->text().isEmpty()) {
                const QModelIndex &src = filterBookmarkModel->mapToSource(index);
                if (ke->key() == Qt::Key_F2) {
                    QStandardItem *item =
                        bookmarkManager->treeBookmarkModel()->itemFromIndex(src);
                    if (item) {
                        item->setEditable(true);
                        treeView->edit(index);
                        item->setEditable(false);
                    }
                } else if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
                    bookmarkManager->removeBookmarkItem(treeView, src);
                }
            }

            switch (ke->key()) {
                default: break;
                case Qt::Key_Up:
                case Qt::Key_Down: {
                    treeView->subclassKeyPressEvent(ke);
                }   break;

                case Qt::Key_Enter:
                case Qt::Key_Return: {
                    index = treeView->selectionModel()->currentIndex();
                    if (index.isValid()) {
                        QString data = index.data(Qt::UserRole + 10).toString();
                        if (!data.isEmpty() && data != QLatin1String("Folder"))
                            emit linkActivated(data);
                    }
                }   break;
            }
        } else if (e->type() == QEvent::MouseButtonRelease) {
            if (index.isValid()) {
                QMouseEvent *me = static_cast<QMouseEvent*>(e);
                bool controlPressed = me->modifiers() & Qt::ControlModifier;
                if (((me->button() == Qt::LeftButton) && controlPressed)
                    || (me->button() == Qt::MiddleButton)) {
                        QString data = index.data(Qt::UserRole + 10).toString();
                        if (!data.isEmpty() && data != QLatin1String("Folder"))
                            emit createPage(QUrl(data), false);
                }
            }
        }
    } else if (object == searchField && e->type() == QEvent::FocusIn) {
        if (static_cast<QFocusEvent *>(e)->reason() != Qt::MouseFocusReason) {
            searchField->selectAll();
            searchField->setFocus();

            QModelIndex index = treeView->indexAt(QPoint(1, 1));
            if (index.isValid())
                treeView->setCurrentIndex(index);
        }
    }
    return QWidget::eventFilter(object, e);
}


// #pragma mark -- BookmarkModel


BookmarkModel::BookmarkModel(int rows, int columns, QObject * parent)
    : QStandardItemModel(rows, columns, parent)
{
}

BookmarkModel::~BookmarkModel()
{
}

Qt::DropActions BookmarkModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

Qt::ItemFlags BookmarkModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags defaultFlags = QStandardItemModel::flags(index);
    if ((!index.isValid()) // can only happen for the invisible root item
        || index.data(Qt::UserRole + 10).toString() == QLatin1String("Folder"))
        return (Qt::ItemIsDropEnabled | defaultFlags) &~ Qt::ItemIsDragEnabled;

    return (Qt::ItemIsDragEnabled | defaultFlags) &~ Qt::ItemIsDropEnabled;
}


// #pragma mark -- BookmarkManager


BookmarkManager::BookmarkManager()
    : m_folderIcon(QApplication::style()->standardIcon(QStyle::SP_DirClosedIcon))
    , m_bookmarkIcon(Utils::Icons::BOOKMARK.icon())
    , treeModel(new BookmarkModel(0, 1, this))
    , listModel(new BookmarkModel(0, 1, this))
{
    connect(treeModel, &BookmarkModel::itemChanged,
            this, &BookmarkManager::itemChanged);
}

BookmarkManager::~BookmarkManager()
{
    treeModel->clear();
    listModel->clear();
}

BookmarkModel* BookmarkManager::treeBookmarkModel() const
{
    return treeModel;
}

BookmarkModel* BookmarkManager::listBookmarkModel() const
{
    return listModel;
}

void BookmarkManager::saveBookmarks()
{
    if (!m_isModelSetup)
        return;
    QByteArray bookmarks;
    QDataStream stream(&bookmarks, QIODevice::WriteOnly);

    readBookmarksRecursive(treeModel->invisibleRootItem(), stream, 0);
    Core::ICore::settings()->setValue(kBookmarksKey, bookmarks);
}

QStringList BookmarkManager::bookmarkFolders() const
{
    QStringList folders(::Help::Tr::tr("Bookmarks"));

    const QList<QStandardItem *> list = treeModel->findItems(QLatin1String("*"),
                                                             Qt::MatchWildcard | Qt::MatchRecursive,
                                                             0);

    QString data;
    for (const QStandardItem *item : list) {
        data = item->data(Qt::UserRole + 10).toString();
        if (data == QLatin1String("Folder"))
            folders << item->data(Qt::DisplayRole).toString();
    }
    return folders;
}

QModelIndex BookmarkManager::addNewFolder(const QModelIndex& index)
{
    QStandardItem *item = new QStandardItem(uniqueFolderName());
    item->setEditable(false);
    item->setIcon(m_folderIcon);
    item->setData(false, Qt::UserRole + 11);
    item->setData(QLatin1String("Folder"), Qt::UserRole + 10);
    item->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirClosedIcon));

    if (index.isValid())
        treeModel->itemFromIndex(index)->appendRow(item);
    else
        treeModel->appendRow(item);
    return treeModel->indexFromItem(item);
}

void BookmarkManager::removeBookmarkItem(QWidget *dialogParent, const QModelIndex &index)
{
    QStandardItem *item = treeModel->itemFromIndex(index);
    if (item) {
        QString data = index.data(Qt::UserRole + 10).toString();
        if (data == QLatin1String("Folder") && item->rowCount() > 0) {
            int value = QMessageBox::question(dialogParent, ::Help::Tr::tr("Remove"),
                ::Help::Tr::tr("Deleting a folder also removes its content.<br>"
                               "Do you want to continue?"),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

            if (value == QMessageBox::Cancel)
                return;
        }

        if (data != QLatin1String("Folder")) {
            const QList<QStandardItem *> itemList = listModel->findItems(item->text());
            for (const QStandardItem *i : itemList) {
                if (i->data(Qt::UserRole + 10) == data) {
                    listModel->removeRow(i->row());
                    break;
                }
            }
        } else {
            removeBookmarkFolderItems(item);
        }
        treeModel->removeRow(item->row(), index.parent());
    }
}

void BookmarkManager::showBookmarkDialog(QWidget* parent, const QString &name,
    const QString &url)
{
    BookmarkDialog dialog(this, name, url, parent);
    dialog.exec();
}

void BookmarkManager::addNewBookmark(const QModelIndex& index,
    const QString &name, const QString &url)
{
    QStandardItem *item = new QStandardItem(name);
    item->setEditable(false);
    item->setIcon(m_bookmarkIcon);
    item->setData(false, Qt::UserRole + 11);
    item->setData(url, Qt::UserRole + 10);

    if (index.isValid())
        treeModel->itemFromIndex(index)->appendRow(item);
    else
        treeModel->appendRow(item);
    listModel->appendRow(item->clone());
}

void BookmarkManager::itemChanged(QStandardItem *item)
{
    if (renameItem != item) {
        renameItem = item;
        oldText = item->text();
        return;
    }

    if (item->text() != oldText) {
        if (item->data(Qt::UserRole + 10).toString() != QLatin1String("Folder")) {
            QList<QStandardItem*>itemList = listModel->findItems(oldText);
            if (!itemList.isEmpty())
                itemList.at(0)->setText(item->text());
        }
    }
}

void BookmarkManager::setupBookmarkModels()
{
    m_isModelSetup = true;
    treeModel->clear();
    listModel->clear();

    qint32 depth;
    bool expanded;
    QString name, type;
    QList<int> lastDepths;
    QList<QStandardItem*> parents;

    QByteArray ba;
    Utils::QtcSettings *settings = Core::ICore::settings();
    ba = settings->value(kBookmarksKey).toByteArray();
    QDataStream stream(ba);
    while (!stream.atEnd()) {
        stream >> depth >> name >> type >> expanded;

        QStandardItem *item = new QStandardItem(name);
        item->setEditable(false);
        item->setData(type, Qt::UserRole + 10);
        item->setData(expanded, Qt::UserRole + 11);
        if (depth == 0) {
            parents.clear(); lastDepths.clear();
            treeModel->appendRow(item);
            parents << item; lastDepths << depth;
        } else {
            if (depth <= lastDepths.last()) {
                while (depth <= lastDepths.last() && parents.count() > 0) {
                    parents.pop_back(); lastDepths.pop_back();
                }
            }
            parents.last()->appendRow(item);
            if (type == QLatin1String("Folder")) {
                parents << item; lastDepths << depth;
            }
        }

        if (type != QLatin1String("Folder")) {
            item->setIcon(m_bookmarkIcon);
            listModel->appendRow(item->clone());
        } else {
            item->setIcon(m_folderIcon);
        }
    }
}

QString BookmarkManager::uniqueFolderName() const
{
    QString folderName = ::Help::Tr::tr("New Folder");
    const QList<QStandardItem *> list = treeModel->findItems(folderName,
                                                             Qt::MatchContains | Qt::MatchRecursive,
                                                             0);
    if (!list.isEmpty()) {
        QStringList names;
        for (const QStandardItem *item : list)
            names << item->text();

        QString folderNameBase = ::Help::Tr::tr("New Folder") + QLatin1String(" %1");
        for (int i = 1; i <= names.count(); ++i) {
            folderName = folderNameBase.arg(i);
            if (!names.contains(folderName))
                break;
        }
    }
    return folderName;
}

void BookmarkManager::removeBookmarkFolderItems(QStandardItem *item)
{
    for (int j = 0; j < item->rowCount(); ++j) {
        QStandardItem *child = item->child(j);
        if (child->rowCount() > 0)
            removeBookmarkFolderItems(child);

        QString data = child->data(Qt::UserRole + 10).toString();
        const QList<QStandardItem*> itemList = listModel->findItems(child->text());
        for (const QStandardItem *i : itemList) {
            if (i->data(Qt::UserRole + 10) == data) {
                listModel->removeRow(i->row());
                break;
            }
        }
    }
}

void BookmarkManager::readBookmarksRecursive(const QStandardItem *item,
    QDataStream &stream, const qint32 depth) const
{
    for (int j = 0; j < item->rowCount(); ++j) {
        const QStandardItem *child = item->child(j);
        stream << depth;
        stream << child->data(Qt::DisplayRole).toString();
        stream << child->data(Qt::UserRole + 10).toString();
        stream << child->data(Qt::UserRole + 11).toBool();

        if (child->rowCount() > 0)
            readBookmarksRecursive(child, stream, (depth +1));
    }
}

#ifdef WITH_TESTS

#include <coreplugin/dialogs/ioptionspage.h>

#include <QSignalSpy>
#include <QTest>

namespace Help::Internal {

class BookmarkDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        BookmarkManager manager;
        BookmarkDialogAspects aspects(&manager, "A page");

        const Utils::Result<> rendered = Core::aspectFormRenders(&aspects, "BookmarkDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheTreeOfFoldersHoldsOnlyFolders()
    {
        BookmarkManager manager;
        BookmarkModel *tree = manager.treeBookmarkModel();
        QStandardItem *folder = tree->itemFromIndex(manager.addNewFolder({}));
        folder->setText("Sample");
        manager.addNewBookmark(folder->index(), "A page", "qthelp://doc/index.html");

        BookmarkDialogAspects aspects(&manager, "A page");
        QAbstractItemModel *rows = aspects.folders.tableModel();
        QVERIFY(rows);
        QCOMPARE(rows->rowCount(QModelIndex()), 1);
        QCOMPARE(rows->index(0, 0).data().toString(), QString("Sample"));
        QVERIFY2(rows->rowCount(rows->index(0, 0)) == 0,
                 "the bookmark itself was offered as somewhere to put a bookmark");
    }

    void testAFolderIsOnlyWritableWhileItIsBeingRenamed()
    {
        // A cell that is always writable is a text field, and then pointing at
        // a folder edits it rather than picking it - which is what this tree
        // is mostly for. So only the row being renamed says it can be written.
        BookmarkManager manager;
        BookmarkModel *tree = manager.treeBookmarkModel();
        tree->itemFromIndex(manager.addNewFolder({}))->setText("Sample");

        BookmarkDialogAspects aspects(&manager, "A page");
        QAbstractItemModel *rows = aspects.folders.tableModel();
        const QModelIndex row = rows->index(0, 0);
        QVERIFY(row.isValid());
        QCOMPARE(row.data(Utils::AspectTable::EditableRole).toBool(), false);
        QCOMPARE(row.data(Utils::AspectTable::CheckableRole).toBool(), false);
        // And the names a Qt Quick view addresses the cell by are there at all.
        QCOMPARE(rows->roleNames().value(Utils::AspectTable::EditableRole), QByteArray("editable"));

        aspects.folders.triggerRowAction(row, QString("rename"));
        QCOMPARE(row.data(Utils::AspectTable::EditableRole).toBool(), true);

        // Writing the name gives it back: nothing is left half-renamed.
        QVERIFY(rows->setData(row, "Renamed", Qt::EditRole));
        QCOMPARE(row.data().toString(), QString("Renamed"));
        QCOMPARE(row.data(Utils::AspectTable::EditableRole).toBool(), false);
        QVERIFY(manager.bookmarkFolders().contains("Renamed"));
    }

    void testTheBoxAndTheTreeSayTheSameThing()
    {
        BookmarkManager manager;
        BookmarkModel *tree = manager.treeBookmarkModel();
        tree->itemFromIndex(manager.addNewFolder({}))->setText("Sample");

        BookmarkDialogAspects aspects(&manager, "A page");
        QAbstractItemModel *rows = aspects.folders.tableModel();
        const QModelIndex row = rows->index(0, 0);

        // Naming a folder in the box puts the reader on it in the tree.
        QSignalSpy shown(&aspects.folders, &Utils::BaseAspect::controlIndexRequested);
        aspects.folder.setValue("Sample");
        QCOMPARE(aspects.chosenFolder(), tree->index(0, 0));
        QCOMPARE(shown.count(), 1);

        // And pointing at one in the tree names it in the box.
        aspects.folders.setCurrentIndex({});
        QCOMPARE(aspects.folder.value(), ::Help::Tr::tr("Bookmarks"));
        aspects.folders.setCurrentIndex(row);
        QCOMPARE(aspects.folder.value(), QString("Sample"));

        // The top of the tree is a folder the box offers but the model has no
        // row for: a bookmark put there goes in at the top.
        QVERIFY(aspects.folder.presentation().choices.size() >= 2);
        aspects.folder.setValue(::Help::Tr::tr("Bookmarks"));
        QVERIFY(!aspects.chosenFolder().isValid());
    }

    void testTheFoldersAreHiddenUntilTheyAreAskedFor()
    {
        BookmarkManager manager;
        BookmarkDialogAspects aspects(&manager, "A page");

        QVERIFY2(!aspects.folders.isVisible(), "the dialog opened on the whole tree");
        QVERIFY(!aspects.newFolder.isVisible());
        QCOMPARE(aspects.showFolders.presentation().actionText, QString("+"));

        aspects.showFolders.triggerAction();
        QVERIFY(aspects.folders.isVisible());
        QVERIFY(aspects.newFolder.isVisible());
        QCOMPARE(aspects.showFolders.presentation().actionText, QString("-"));

        aspects.showFolders.triggerAction();
        QVERIFY(!aspects.folders.isVisible());
        QCOMPARE(aspects.showFolders.presentation().actionText, QString("+"));
    }

    void testANewFolderIsMadeWhereTheReaderIs()
    {
        BookmarkManager manager;
        BookmarkModel *tree = manager.treeBookmarkModel();
        tree->itemFromIndex(manager.addNewFolder({}))->setText("Sample");

        BookmarkDialogAspects aspects(&manager, "A page");
        QAbstractItemModel *rows = aspects.folders.tableModel();
        aspects.folders.setCurrentIndex(rows->index(0, 0));

        aspects.newFolder.triggerAction();
        QCOMPARE(tree->item(0)->rowCount(), 1);
        // And the reader is moved onto it, so the next one goes below it.
        QCOMPARE(aspects.chosenFolder(), tree->item(0)->child(0)->index());
        QCOMPARE(aspects.folder.value(), tree->item(0)->child(0)->text());
    }

    void testRemovingAFolderLeavesNothingPointingAtIt()
    {
        BookmarkManager manager;
        BookmarkModel *tree = manager.treeBookmarkModel();
        tree->itemFromIndex(manager.addNewFolder({}))->setText("Sample");

        BookmarkDialogAspects aspects(&manager, "A page");
        QAbstractItemModel *rows = aspects.folders.tableModel();
        aspects.folders.setCurrentIndex(rows->index(0, 0));
        QCOMPARE(aspects.folder.value(), QString("Sample"));

        aspects.folders.triggerRowAction(rows->index(0, 0), QString("delete"));
        QCOMPARE(tree->rowCount(), 0);
        QVERIFY2(!aspects.chosenFolder().isValid(),
                 "a bookmark would have been put in a folder that is gone");
        QCOMPARE(aspects.folder.value(), ::Help::Tr::tr("Bookmarks"));
        QVERIFY(!aspects.folder.presentation().choices.isEmpty());
    }
};

QObject *createBookmarkDialogTest()
{
    return new BookmarkDialogTest;
}

} // namespace Help::Internal

#include "bookmarkmanager.moc"

#endif // WITH_TESTS

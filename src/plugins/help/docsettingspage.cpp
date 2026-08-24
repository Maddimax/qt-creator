// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "docsettingspage.h"

#include "helpmanager.h"
#include "helptr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/algorithm.h>
#include <utils/aspectpresentation.h>
#include <utils/aspects.h>
#include <utils/filedialogs.h>
#include <utils/fileutils.h>

#include <QAbstractListModel>
#include <QDir>
#include <QMessageBox>
#include <QVariant>
#include <QVector>

#include <algorithm>

using namespace Utils;

namespace Help::Internal {

class DocEntry final
{
public:
    QString name;
    QString fileName;
    QString nameSpace;

    friend bool operator<(const DocEntry &d1, const DocEntry &d2) { return d1.name < d2.name; }
};

static DocEntry createEntry(const QString &nameSpace, const QString &fileName, bool userManaged)
{
    DocEntry result;
    result.name = userManaged ? nameSpace : Tr::tr("%1 (auto-detected)").arg(nameSpace);
    result.fileName = fileName;
    result.nameSpace = nameSpace;
    return result;
}

using NameSpaceToPathHash = QMultiHash<QString, FilePath>;

// The page's working state: which namespaces should be registered when Apply
// comes, and which should be taken away. Removing a row is deferred that way,
// which is what the page did with two hashes beside its list.
class DocModel final : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;

    // What is registered now, as the page was opened.
    void reload();

    int rowCount(const QModelIndex & = QModelIndex()) const final { return m_docEntries.size(); }
    int columnCount(const QModelIndex & = QModelIndex()) const final { return 1; }
    QVariant data(const QModelIndex &index, int role) const final;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const final;
    QHash<int, QByteArray> roleNames() const final;
    bool removeRows(int row, int count, const QModelIndex &parent = {}) final;

    // Adds \a files, and says which of them it could not: a file with no
    // namespace, or a namespace that is registered already.
    NameSpaceToPathHash addFiles(const FilePaths &files);

    void apply();

private:
    void insertEntry(const DocEntry &e);

    QVector<DocEntry> m_docEntries;
    NameSpaceToPathHash m_filesToRegister;
    QHash<QString, bool> m_filesToRegisterUserManaged;
    NameSpaceToPathHash m_filesToUnregister;
};

QVariant DocModel::data(const QModelIndex &index, int role) const
{
    const int row = index.row();
    if (!index.isValid() || row >= m_docEntries.size())
        return {};

    switch (role) {
    case Qt::DisplayRole:
        return m_docEntries.at(row).name;
    case Qt::ToolTipRole:
        return QDir::toNativeSeparators(m_docEntries.at(row).fileName);
    case Qt::UserRole:
        return m_docEntries.at(row).nameSpace;
    // A registered file is added and removed, never typed over.
    case AspectTable::EditableRole:
    case AspectTable::CheckableRole:
        return false;
    }
    return {};
}

QVariant DocModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0)
        return Tr::tr("Documentation");
    return {};
}

QHash<int, QByteArray> DocModel::roleNames() const
{
    return AspectTable::withRoleNames(QAbstractListModel::roleNames());
}

void DocModel::reload()
{
    beginResetModel();
    m_docEntries.clear();
    m_filesToRegister.clear();
    m_filesToRegisterUserManaged.clear();
    m_filesToUnregister.clear();

    const QStringList nameSpaces = HelpManager::registeredNamespaces();
    const QSet<FilePath> userDocumentationPaths = HelpManager::userDocumentationPaths();
    m_docEntries.reserve(nameSpaces.size());
    for (const QString &nameSpace : nameSpaces) {
        const FilePath filePath = HelpManager::fileFromNamespace(nameSpace);
        const bool user = userDocumentationPaths.contains(filePath);
        m_docEntries.append(createEntry(nameSpace, filePath.path(), user));
        m_filesToRegister.insert(nameSpace, filePath);
        m_filesToRegisterUserManaged.insert(nameSpace, user);
    }
    std::stable_sort(m_docEntries.begin(), m_docEntries.end());
    endResetModel();
}

void DocModel::insertEntry(const DocEntry &e)
{
    const auto it = std::lower_bound(m_docEntries.begin(), m_docEntries.end(), e);
    const int index = int(it - m_docEntries.begin());
    beginInsertRows(QModelIndex(), index, index);
    m_docEntries.insert(it, e);
    endInsertRows();
}

bool DocModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || row + count > m_docEntries.size())
        return false;

    beginRemoveRows({}, row, row + count - 1);
    for (int i = row + count - 1; i >= row; --i) {
        const QString nameSpace = m_docEntries.at(i).nameSpace;
        m_filesToRegister.remove(nameSpace);
        m_filesToRegisterUserManaged.remove(nameSpace);
        m_filesToUnregister.insert(nameSpace,
                                   HelpManager::fileFromNamespace(nameSpace).cleanPath());
        m_docEntries.removeAt(i);
    }
    endRemoveRows();
    return true;
}

NameSpaceToPathHash DocModel::addFiles(const FilePaths &files)
{
    NameSpaceToPathHash unableToRegister;
    for (const FilePath &file : files) {
        const QString filePath = file.cleanPath().path();
        const QString nameSpace = HelpManager::namespaceFromFile(filePath);
        if (nameSpace.isEmpty()) {
            unableToRegister.insert("UnknownNamespace", file);
            continue;
        }
        if (m_filesToRegister.contains(nameSpace)) {
            unableToRegister.insert(nameSpace, file);
            continue;
        }

        insertEntry(createEntry(nameSpace, file.toUrlishString(), true /* user managed */));
        m_filesToRegister.insert(nameSpace, file.cleanPath());
        m_filesToRegisterUserManaged.insert(nameSpace, true /* user managed */);

        // If the files to unregister contains the namespace, grab a copy of all paths added and try to
        // remove the current file path. Afterwards remove the whole entry and add the clean list back.
        // Possible outcome:
        //      - might not add the entry back at all if we register the same file again
        //      - might add the entry back with paths to other files with the same namespace
        // The reason to do this is, if we remove a file with a given namespace/ path and re-add another
        // file with the same namespace but a different path, we need to unregister the namespace before
        // we can register the new one. Help engine allows just one registered namespace.
        if (m_filesToUnregister.contains(nameSpace)) {
            QSet<FilePath> values = Utils::toSet(m_filesToUnregister.values(nameSpace));
            values.remove(file.cleanPath());
            m_filesToUnregister.remove(nameSpace);
            for (const FilePath &value : std::as_const(values))
                m_filesToUnregister.insert(nameSpace, value);
        }
    }
    return unableToRegister;
}

void DocModel::apply()
{
    HelpManager::instance()->unregisterDocumentation(m_filesToUnregister.values());
    FilePaths files;
    for (auto it = m_filesToRegisterUserManaged.constBegin();
         it != m_filesToRegisterUserManaged.constEnd(); ++it) {
        if (it.value() /*userManaged*/)
            files << m_filesToRegister.value(it.key());
    }
    HelpManager::registerUserDocumentation(files);
    m_filesToUnregister.clear();
}

class DocsAspect final : public BaseAspect
{
public:
    explicit DocsAspect(AspectContainer *container)
        : BaseAspect(container)
        // Parented: a model handed to QML from a Q_INVOKABLE with no parent is
        // one QML takes ownership of and deletes.
        , m_model(new DocModel(this))
    {
        setQmlName("Docs");
        m_model->reload();
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &BaseAspect::volatileValueChanged);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, &BaseAspect::volatileValueChanged);
    }

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        // A .qch is added through the file dialog that finds it.
        p.allowAdding = false;
        p.allowRemoving = true;
        p.filterPlaceholderText = Tr::tr("Filter");
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

    DocModel *model() const { return m_model; }

    void apply() override { m_model->apply(); }
    void cancel() override { m_model->reload(); }

private:
    DocModel *m_model = nullptr;
};

// What the Documentation page edits. The registered help files live in
// HelpManager, so the model is a working copy of them.
class DocSettings final : public AspectContainer
{
public:
    DocSettings()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Help/DocSettingsPage.qml"));

        m_docs.setToolTip(Tr::tr("Add and remove compressed help files, .qch."));

        m_add.setQmlName("AddDocumentation");
        m_add.setActionText(Tr::tr("Add..."));
        m_add.setAction([this] { addDocumentation(); });
    }

private:
    void addDocumentation()
    {
        const FilePaths files = FileUtils::getOpenFilePaths(Tr::tr("Add Documentation"),
                                                           m_recentDialogPath,
                                                           Tr::tr("Qt Help Files (*.qch)"));
        if (files.isEmpty())
            return;
        m_recentDialogPath = files.first().canonicalPath();

        NameSpaceToPathHash rest = m_docs.model()->addFiles(files);
        if (rest.isEmpty())
            return;

        QString formatedFail;
        if (rest.contains("UnknownNamespace")) {
            formatedFail += QString::fromLatin1("<ul><li><b>%1</b>")
                                .arg(Tr::tr("Invalid documentation file:"));
            const FilePaths values = rest.values("UnknownNamespace");
            for (const FilePath &value : values)
                formatedFail += QString::fromLatin1("<ul><li>%2</li></ul>").arg(value.toUserOutput());
            formatedFail += "</li></ul>";
            rest.remove("UnknownNamespace");
        }

        if (!rest.isEmpty()) {
            formatedFail += QString::fromLatin1("<ul><li><b>%1</b>")
                                .arg(Tr::tr("Namespace already registered:"));
            for (auto it = rest.constBegin(), end = rest.constEnd(); it != end; ++it) {
                formatedFail += QString::fromLatin1("<ul><li>%1 - %2</li></ul>")
                                    .arg(it.key(), it.value().toUserOutput());
            }
            formatedFail += "</li></ul>";
        }

        QMessageBox::information(Core::ICore::dialogParent(),
                                 Tr::tr("Registration Failed"),
                                 Tr::tr("Unable to register documentation.") + formatedFail,
                                 QMessageBox::Ok);
    }

    DocsAspect m_docs{this};
    ActionAspect m_add{this};
    FilePath m_recentDialogPath;
};

class DocSettingsPage final : public Core::IOptionsPage
{
public:
    DocSettingsPage()
    {
        setId("B.Documentation");
        setDisplayName(Tr::tr("Documentation"));
        setCategory(Core::Constants::HELP_CATEGORY);
        setSettingsProvider([] {
            static DocSettings theSettings;
            return &theSettings;
        });
    }
};

void setupDocSettingsPage()
{
    static DocSettingsPage theDocSettingsPage;
}

} // namespace Help::Internal

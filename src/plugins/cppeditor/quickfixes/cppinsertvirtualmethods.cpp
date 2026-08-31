// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppinsertvirtualmethods.h"

#include "../cppcodestylesettings.h"
#include "../cppeditortr.h"
#include "../cpptoolsreuse.h"
#include "../functionutils.h"
#include "../insertionpointlocator.h"
#include "cppquickfixassistant.h"
#include "cppquickfixhelpers.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <texteditor/fontsettings.h>

#include <cplusplus/CppRewriter.h>
#include <cplusplus/Overview.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/changeset.h>
#include <utils/qtcassert.h>
#include <utils/utilsicons.h>

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPointer>
#include <QQueue>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTextDocument>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#include <QTest>
#endif

using namespace CPlusPlus;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor::Internal {

class InsertVirtualMethodsAspects;
class InsertVirtualMethodsModel;
class VirtualMethodsSettings;

class InsertVirtualMethodsDialog : public QDialog
{
    Q_OBJECT
public:
    enum CustomItemRoles {
        Reimplemented = Qt::UserRole
    };

    enum ImplementationMode {
        ModeOnlyDeclarations = 0x00000001,
        ModeInsideClass = 0x00000002,
        ModeOutsideClass = 0x00000004,
        ModeImplementationFile = 0x00000008
    };

    InsertVirtualMethodsDialog(QWidget *parent = nullptr);
    ~InsertVirtualMethodsDialog() override;
    void initGui();
    void initData();
    virtual void saveSettings();
    const VirtualMethodsSettings *settings() const;

    void setHasImplementationFile(bool file);
    void setHasReimplementedFunctions(bool functions);

    virtual bool gather();

private:
    std::unique_ptr<InsertVirtualMethodsAspects> m_aspects;
    QDialogButtonBox *m_buttons = nullptr;
    bool m_hasImplementationFile = false;
    bool m_hasReimplementedFunctions = false;

protected:
    VirtualMethodsSettings *m_settings;

public:
    InsertVirtualMethodsModel *classFunctionModel;
    QSortFilterProxyModel *classFunctionFilterModel;
};

void registerInsertVirtualMethodsQuickfix()
{
    CppQuickFixFactory::registerFactory<InsertVirtualMethods>();
}

} // namespace CppEditor::Internal

Q_DECLARE_METATYPE(CppEditor::Internal::InsertVirtualMethodsDialog::ImplementationMode)

namespace {

class InsertVirtualMethodsItem
{
public:
    InsertVirtualMethodsItem(InsertVirtualMethodsItem *parent)
        : m_parent(parent)
    {}

    virtual ~InsertVirtualMethodsItem() = default;

    virtual QString description() const = 0;
    virtual Qt::ItemFlags flags() const = 0;
    virtual Qt::CheckState checkState() const = 0;

    InsertVirtualMethodsItem *parent() { return m_parent; }

    int row = -1;

private:
    InsertVirtualMethodsItem *m_parent = nullptr;
};

class FunctionItem;

class ClassItem : public InsertVirtualMethodsItem
{
public:
    ClassItem(const QString &className, const Class *clazz);
    ~ClassItem() override;

    QString description() const override { return name; }
    Qt::ItemFlags flags() const override;
    Qt::CheckState checkState() const override;
    void removeFunction(int row);

    const Class *klass;
    const QString name;
    QList<FunctionItem *> functions;
};

class FunctionItem : public InsertVirtualMethodsItem
{
public:
    FunctionItem(const Function *func, const QString &functionName, ClassItem *parent);
    QString description() const override;
    Qt::ItemFlags flags() const override;
    Qt::CheckState checkState() const override { return checked ? Qt::Checked : Qt::Unchecked; }

    const Function *function = nullptr;
    CppEditor::InsertionPointLocator::AccessSpec accessSpec
        = CppEditor::InsertionPointLocator::Invalid;
    bool reimplemented = false;
    bool alreadyFound = false;
    bool checked = false;
    FunctionItem *nextOverride = nullptr;

private:
    QString name;
};

ClassItem::ClassItem(const QString &className, const Class *clazz) :
    InsertVirtualMethodsItem(nullptr),
    klass(clazz),
    name(className)
{
}

ClassItem::~ClassItem()
{
    qDeleteAll(functions);
    functions.clear();
}

Qt::ItemFlags ClassItem::flags() const
{
    for (FunctionItem *func : std::as_const(functions)) {
        if (!func->alreadyFound)
            return Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled;
    }

    return Qt::ItemIsSelectable;
}

Qt::CheckState ClassItem::checkState() const
{
    if (functions.isEmpty())
        return Qt::Unchecked;
    Qt::CheckState state = functions.first()->checkState();
    for (FunctionItem *function : std::as_const(functions)) {
        Qt::CheckState functionState = function->checkState();
        if (functionState != state)
            return Qt::PartiallyChecked;
    }
    return state;
}

void ClassItem::removeFunction(int row)
{
    QTC_ASSERT(row >= 0 && row < functions.count(), return);
    functions.removeAt(row);
    // Update row number for all the following functions
    for (int r = row, total = functions.count(); r < total; ++r)
        functions[r]->row = r;
}

FunctionItem::FunctionItem(const Function *func, const QString &functionName, ClassItem *parent) :
    InsertVirtualMethodsItem(parent),
    function(func),
    nextOverride(this)
{
    name = functionName;
}

QString FunctionItem::description() const
{
    return name;
}

Qt::ItemFlags FunctionItem::flags() const
{
    Qt::ItemFlags res = Qt::NoItemFlags;
    if (!alreadyFound)
        res |= Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled;
    return res;
}

QStringList defaultOverrideReplacements()
{
    return {
        QLatin1String("override"),
        QLatin1String("Q_DECL_OVERRIDE")
    };
}

// Which row of the list the dialog builds the next time it opens holds the
// replacement that was chosen, so that it opens on it. The widget dialog
// stored the row the list had before what was typed was added to it, which was
// a different list.
int overrideReplacementRow(const QString &chosen, const QStringList &userAdded)
{
    return qMax(0, (defaultOverrideReplacements() + userAdded).indexOf(chosen));
}

QStringList sortedAndTrimmedStringListWithoutEmptyElements(const QStringList &list)
{
    QStringList result;
    for (const QString &replacement : list) {
        const QString trimmedReplacement = replacement.trimmed();
        if (!trimmedReplacement.isEmpty())
            result << trimmedReplacement;
    }
    result.sort();
    return result;
}

} // namespace

namespace CppEditor::Internal {

const bool kInsertVirtualKeywordDefault = false;
const bool kHideReimplementedFunctionsDefault = false;
const bool kInsertOVerrideReplacementDefault = false;
const int kOverrideReplacementIndexDefault = 0;
const InsertVirtualMethodsDialog::ImplementationMode kImplementationModeDefault
    = InsertVirtualMethodsDialog::ModeOnlyDeclarations;

class VirtualMethodsSettings
{
public:
    void read()
    {
        QtcSettings *s = Core::ICore::settings();
        s->beginGroup(group());
        insertVirtualKeyword = s->value(insertVirtualKeywordKey(), kInsertVirtualKeywordDefault)
                                   .toBool();
        hideReimplementedFunctions
            = s->value(hideReimplementedFunctionsKey(), kHideReimplementedFunctionsDefault).toBool();
        insertOverrideReplacement
            = s->value(insertOverrideReplacementKey(), kInsertOVerrideReplacementDefault).toBool();
        overrideReplacementIndex
            = s->value(overrideReplacementIndexKey(), kOverrideReplacementIndexDefault).toInt();
        userAddedOverrideReplacements = s->value(userAddedOverrideReplacementsKey()).toStringList();
        implementationMode = static_cast<InsertVirtualMethodsDialog::ImplementationMode>(
            s->value(implementationModeKey(), int(kImplementationModeDefault)).toInt());
        s->endGroup();
    }

    void write() const
    {
        QtcSettings *s = Core::ICore::settings();
        s->beginGroup(group());
        s->setValueWithDefault(insertVirtualKeywordKey(),
                               insertVirtualKeyword,
                               kInsertVirtualKeywordDefault);
        s->setValueWithDefault(hideReimplementedFunctionsKey(),
                               hideReimplementedFunctions,
                               kHideReimplementedFunctionsDefault);
        s->setValueWithDefault(insertOverrideReplacementKey(),
                               insertOverrideReplacement,
                               kInsertOVerrideReplacementDefault);
        s->setValueWithDefault(overrideReplacementIndexKey(),
                               overrideReplacementIndex,
                               kOverrideReplacementIndexDefault);
        s->setValueWithDefault(userAddedOverrideReplacementsKey(), userAddedOverrideReplacements);
        s->setValueWithDefault(implementationModeKey(),
                               int(implementationMode),
                               int(kImplementationModeDefault));
        s->endGroup();
    }

    QString overrideReplacement; // internal
    QStringList userAddedOverrideReplacements;
    InsertVirtualMethodsDialog::ImplementationMode implementationMode = kImplementationModeDefault;
    int overrideReplacementIndex = kOverrideReplacementIndexDefault;
    bool insertVirtualKeyword = kInsertVirtualKeywordDefault;
    bool hideReimplementedFunctions = kHideReimplementedFunctionsDefault;
    bool insertOverrideReplacement = kInsertOVerrideReplacementDefault;

private:
    static Key group() { return "QuickFix/InsertVirtualMethods"; }
    static Key insertVirtualKeywordKey() { return "insertKeywordVirtual"; }
    static Key insertOverrideReplacementKey() { return "insertOverrideReplacement"; }
    static Key overrideReplacementIndexKey() { return "overrideReplacementIndex"; }
    static Key userAddedOverrideReplacementsKey() { return "userAddedOverrideReplacements"; }
    static Key implementationModeKey() { return "implementationMode"; }
    static Key hideReimplementedFunctionsKey() { return "hideReimplementedFunctions"; }
};

class InsertVirtualMethodsModel : public QAbstractItemModel
{
public:
    InsertVirtualMethodsModel(QObject *parent = nullptr) : QAbstractItemModel(parent)
    {
        const FontSettingsData fs = globalFontSettings().data();
        formatReimpFunc = fs.formatFor(C_DISABLED_CODE);
    }

    ~InsertVirtualMethodsModel() override
    {
        clear();
    }

    void clear()
    {
        beginResetModel();
        qDeleteAll(classes);
        classes.clear();
        endResetModel();
    }

    QModelIndex index(int row, int column, const QModelIndex &parent) const override
    {
        if (column != 0)
            return {};
        if (parent.isValid()) {
            auto classItem = static_cast<ClassItem *>(parent.internalPointer());
            return createIndex(row, column, classItem->functions.at(row));
        }
        return createIndex(row, column, classes.at(row));
    }

    QModelIndex parent(const QModelIndex &child) const override
    {
        if (!child.isValid())
            return {};
        InsertVirtualMethodsItem *parent = itemForIndex(child)->parent();
        return parent ? createIndex(parent->row, 0, parent) : QModelIndex();
    }

    int rowCount(const QModelIndex &parent) const override
    {
        if (!parent.isValid())
            return classes.count();
        InsertVirtualMethodsItem *item = itemForIndex(parent);
        if (item->parent()) // function -> no children
            return 0;
        return static_cast<ClassItem *>(item)->functions.count();
    }

    int columnCount(const QModelIndex &) const override
    {
        return 1;
    }

    void addClass(ClassItem *classItem)
    {
        int row = classes.count();
        classItem->row = row;
        beginInsertRows(QModelIndex(), row, row);
        classes.append(classItem);
        endInsertRows();
    }

    void removeFunction(FunctionItem *funcItem)
    {
        auto classItem = static_cast<ClassItem *>(funcItem->parent());
        beginRemoveRows(createIndex(classItem->row, 0, classItem), funcItem->row, funcItem->row);
        classItem->removeFunction(funcItem->row);
        endRemoveRows();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid())
            return QVariant();

        InsertVirtualMethodsItem *item = itemForIndex(index);
        switch (role) {
        case Qt::DisplayRole:
            return item->description();
        case Qt::CheckStateRole:
            return item->checkState();
        case Qt::ForegroundRole:
            if (item->parent() && static_cast<FunctionItem *>(item)->alreadyFound)
                return formatReimpFunc.foreground();
            break;
        case Qt::BackgroundRole:
            if (item->parent() && static_cast<FunctionItem *>(item)->alreadyFound) {
                const QColor background = formatReimpFunc.background();
                if (background.isValid())
                    return background;
            }
            break;
        case InsertVirtualMethodsDialog::Reimplemented:
            if (item->parent()) {
                auto function = static_cast<FunctionItem *>(item);
                return QVariant(function->alreadyFound);
            }
            break;
        // Every row is a check box, and a function already reimplemented is
        // one the reader cannot pick - which is what its flags say.
        case AspectTable::CheckableRole:
            return true;
        case AspectTable::EditableRole:
            return AspectTable::isWritable(item->flags());
        }
        return QVariant();
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (!index.isValid())
            return false;

        InsertVirtualMethodsItem *item = itemForIndex(index);
        switch (role) {
        case Qt::CheckStateRole: {
            bool checked = value.toInt() == Qt::Checked;
            if (item->parent()) {
                auto funcItem = static_cast<FunctionItem *>(item);
                while (funcItem->checked != checked) {
                    funcItem->checked = checked;
                    const QModelIndex funcIndex = createIndex(funcItem->row, 0, funcItem);
                    emit dataChanged(funcIndex, funcIndex);
                    const QModelIndex parentIndex =
                            createIndex(funcItem->parent()->row, 0, funcItem->parent());
                    emit dataChanged(parentIndex, parentIndex);
                    funcItem = funcItem->nextOverride;
                }
            } else {
                auto classItem = static_cast<ClassItem *>(item);
                for (FunctionItem *funcItem : std::as_const(classItem->functions)) {
                    if (funcItem->alreadyFound || funcItem->checked == checked)
                        continue;
                    QModelIndex funcIndex = createIndex(funcItem->row, 0, funcItem);
                    setData(funcIndex, value, role);
                }
            }
            return true;
        }
        }
        return QAbstractItemModel::setData(index, value, role);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::NoItemFlags;
        return itemForIndex(index)->flags();
    }

    // The one column has no name, and a Qt Quick view draws a heading for a
    // column that answers one.
    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractItemModel::roleNames());
    }

    QList<ClassItem *> classes;

private:
    Format formatReimpFunc;

    InsertVirtualMethodsItem *itemForIndex(const QModelIndex &index) const
    {
        return static_cast<InsertVirtualMethodsItem *>(index.internalPointer());
    }
};

class InsertVirtualMethodsOp : public CppQuickFixOperation
{
public:
    InsertVirtualMethodsOp(const CppQuickFixInterface &interface,
                           InsertVirtualMethodsDialog *factory)
        : CppQuickFixOperation(interface, 0)
        , m_factory(factory)
    {
        setDescription(Tr::tr("Insert Virtual Functions of Base Classes"));

        const QList<AST *> &path = interface.path();
        const int pathSize = path.size();
        if (pathSize < 2)
            return;

        // Determine if cursor is on a class or a base class
        if (SimpleNameAST *nameAST = path.at(pathSize - 1)->asSimpleName()) {
            if (!interface.isCursorOn(nameAST))
                return;

            if (!(m_classAST = path.at(pathSize - 2)->asClassSpecifier())) { // normal class
                int index = pathSize - 2;
                const BaseSpecifierAST *baseAST = path.at(index)->asBaseSpecifier();// simple bclass
                if (!baseAST) {
                    if (index > 0 && path.at(index)->asQualifiedName()) // namespaced base class
                        baseAST = path.at(--index)->asBaseSpecifier();
                }
                --index;
                if (baseAST && index >= 0)
                    m_classAST = path.at(index)->asClassSpecifier();
            }
        }

        // Also offer the operation if we are on some "empty" part of the class declaration.
        if (!m_classAST)
            m_classAST = path.at(pathSize - 1)->asClassSpecifier();

        if (!m_classAST || !m_classAST->base_clause_list)
            return;

        // Determine insert positions
        const int endOfClassAST = interface.currentFile()->endOf(m_classAST);
        m_insertPosDecl = endOfClassAST - 1; // Skip last "}"
        m_insertPosOutside = endOfClassAST + 1; // Step over ";"

        // Determine base classes
        QList<const Class *> baseClasses;
        QQueue<ClassOrNamespace *> baseClassQueue;
        QSet<ClassOrNamespace *> visitedBaseClasses;
        if (ClassOrNamespace *clazz = interface.context().lookupType(m_classAST->symbol))
            baseClassQueue.enqueue(clazz);
        while (!baseClassQueue.isEmpty()) {
            ClassOrNamespace *clazz = baseClassQueue.dequeue();
            visitedBaseClasses.insert(clazz);
            QList<ClassOrNamespace *> bases = clazz->usings();
            while (!bases.isEmpty()) {
                const ClassOrNamespace *baseClass = bases.takeFirst();
                const QList<Symbol *> symbols = baseClass->symbols();

                // See QTCREATORBUG-32162.
                if (symbols.isEmpty() && baseClass->usings().size() == 1) {
                    bases.prepend(baseClass->usings().first());
                    continue;
                }

                for (Symbol *symbol : symbols) {
                    Class *base = symbol->asClass();
                    if (base
                            && (clazz = interface.context().lookupType(symbol))
                            && !visitedBaseClasses.contains(clazz)
                            && !baseClasses.contains(base)) {
                        baseClasses.prepend(base);
                        baseClassQueue.enqueue(clazz);
                    }
                }
            }
        }

        // Determine virtual functions
        m_factory->classFunctionModel->clear();
        Overview printer = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        printer.showFunctionSignatures = true;
        QHash<const Function *, FunctionItem *> virtualFunctions;
        for (const Class *clazz : std::as_const(baseClasses)) {
            ClassItem *itemBase = new ClassItem(printer.prettyName(clazz->name()), clazz);
            for (Scope::iterator it = clazz->memberBegin(); it != clazz->memberEnd(); ++it) {
                if (const Function *func = (*it)->type()->asFunctionType()) {
                    // Filter virtual destructors
                    const Name *name = func->name();
                    if (!name || name->asDestructorNameId())
                        continue;

                    QList<const Function * > firstVirtuals;
                    const bool isVirtual = FunctionUtils::isVirtualFunction(
                                func, interface.context(), &firstVirtuals);
                    if (!isVirtual)
                        continue;

                    if (func->isFinal()) {
                        for (const Function *firstVirtual : std::as_const(firstVirtuals)) {
                            if (FunctionItem *first = virtualFunctions[firstVirtual]) {
                                FunctionItem *next = nullptr;
                                for (FunctionItem *removed = first; next != first; removed = next) {
                                    next = removed->nextOverride;
                                    m_factory->classFunctionModel->removeFunction(removed);
                                    delete removed;
                                };
                                virtualFunctions.remove(firstVirtual);
                            }
                        }
                        continue;
                    }
                    // Filter OQbject's
                    //   - virtual const QMetaObject *metaObject() const;
                    //   - virtual void *qt_metacast(const char *);
                    //   - virtual int qt_metacall(QMetaObject::Call, int, void **);
                    bool skip = false;
                    for (const Function *firstVirtual : std::as_const(firstVirtuals)) {
                        if (printer.prettyName(firstVirtual->enclosingClass()->name()) == "QObject"
                                && magicQObjectFunctions().contains(
                                    printer.prettyName(func->name()))) {
                            skip = true;
                            break;
                        }
                    }
                    if (skip)
                        continue;

                    // Do not implement existing functions inside target class
                    bool funcExistsInClass = false;
                    const Name *funcName = func->name();
                    const OperatorNameId * const opName = funcName->asOperatorNameId();
                    Symbol *symbol = opName ? m_classAST->symbol->find(opName->kind())
                                            : m_classAST->symbol->find(funcName->identifier());
                    for (; symbol; symbol = symbol->next()) {
                        if (!opName && (!symbol->name()
                                        || !funcName->identifier()->match(symbol->identifier()))) {
                            continue;
                        }
                        if (symbol->type().match(func->type())) {
                            funcExistsInClass = true;
                            break;
                        }
                    }

                    // Construct function item
                    const bool isReimplemented = !firstVirtuals.contains(func);
                    const bool isPureVirtual = func->isPureVirtual();
                    QString itemName = printer.prettyType(func->type(), func->name());
                    if (isPureVirtual)
                        itemName += QLatin1String(" = 0");
                    const QString itemReturnTypeString = printer.prettyType(func->returnType());
                    itemName += QLatin1String(" : ") + itemReturnTypeString;
                    if (isReimplemented)
                        itemName += QLatin1String(" (redeclared)");
                    auto funcItem = new FunctionItem(func, itemName, itemBase);
                    if (isReimplemented) {
                        factory->setHasReimplementedFunctions(true);
                        funcItem->reimplemented = true;
                        funcItem->alreadyFound = funcExistsInClass;
                        for (const Function *firstVirtual : std::as_const(firstVirtuals)) {
                            if (FunctionItem *first = virtualFunctions[firstVirtual]) {
                                if (!first->alreadyFound) {
                                    while (first->checked != isPureVirtual) {
                                        first->checked = isPureVirtual;
                                        first = first->nextOverride;
                                    }
                                }
                                funcItem->checked = first->checked;

                                FunctionItem *prev = funcItem;
                                for (FunctionItem *next = funcItem->nextOverride;
                                     next && next != funcItem; next = next->nextOverride) {
                                    prev = next;
                                }
                                prev->nextOverride = first->nextOverride;
                                first->nextOverride = funcItem;
                            }
                        }
                    } else {
                        if (!funcExistsInClass) {
                            funcItem->checked = isPureVirtual;
                        } else {
                            funcItem->alreadyFound = true;
                            funcItem->checked = true;
                            factory->setHasReimplementedFunctions(true);
                        }
                    }

                    funcItem->accessSpec = acessSpec(*it);
                    funcItem->row = itemBase->functions.count();
                    itemBase->functions.append(funcItem);

                    virtualFunctions[func] = funcItem;

                    // update internal counters
                    if (!funcExistsInClass)
                        ++m_functionCount;
                }
            }

            if (itemBase->functions.isEmpty())
                delete itemBase;
            else
                m_factory->classFunctionModel->addClass(itemBase);
        }
        if (m_factory->classFunctionModel->classes.isEmpty() || m_functionCount == 0)
            return;

        bool isHeaderFile = false;
        m_cppFilePath = correspondingHeaderOrSource(interface.filePath(), &isHeaderFile);
        m_factory->setHasImplementationFile(isHeaderFile && !m_cppFilePath.isEmpty());

        m_valid = true;
    }

    bool isValid() const
    {
        return m_valid;
    }

    InsertionPointLocator::AccessSpec acessSpec(const Symbol *symbol)
    {
        const Function *func = symbol->type()->asFunctionType();
        if (!func)
            return InsertionPointLocator::Invalid;
        if (func->isSignal())
            return InsertionPointLocator::Signals;

        InsertionPointLocator::AccessSpec spec = InsertionPointLocator::Invalid;
        if (symbol->isPrivate())
            spec = InsertionPointLocator::Private;
        else if (symbol->isProtected())
            spec = InsertionPointLocator::Protected;
        else if (symbol->isPublic())
            spec = InsertionPointLocator::Public;
        else
            return InsertionPointLocator::Invalid;

        if (func->isSlot()) {
            switch (spec) {
            case InsertionPointLocator::Private:
                return InsertionPointLocator::PrivateSlot;
            case InsertionPointLocator::Protected:
                return InsertionPointLocator::ProtectedSlot;
            case InsertionPointLocator::Public:
                return InsertionPointLocator::PublicSlot;
            default:
                return spec;
            }
        }
        return spec;
    }

    void perform() override
    {
        if (!m_factory->gather())
            return;

        m_factory->saveSettings();

        // Insert declarations (and definition if Inside-/OutsideClass)
        Overview printer = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        printer.showFunctionSignatures = true;
        printer.showReturnTypes = true;
        printer.showArgumentNames = true;
        printer.showTemplateParameters = true;
        Utils::ChangeSet headerChangeSet;
        const CppRefactoringChanges refactoring(snapshot());
        const CppRefactoringFilePtr headerFile = currentFile();
        const LookupContext targetContext(headerFile->cppDocument(), snapshot());

        const Class *targetClass = m_classAST->symbol;
        ClassOrNamespace *targetCoN = targetContext.lookupType(targetClass->enclosingScope());
        if (!targetCoN)
            targetCoN = targetContext.globalNamespace();
        UseMinimalNames useMinimalNames(targetCoN);
        Control *control = context().bindings()->control().get();
        QList<const Function *> insertedFunctions;
        for (ClassItem *classItem : std::as_const(m_factory->classFunctionModel->classes)) {
            if (classItem->checkState() == Qt::Unchecked)
                continue;

            // Insert Declarations (+ definitions)
            QString lastAccessSpecString;
            bool first = true;
            for (FunctionItem *funcItem : std::as_const(classItem->functions)) {
                if (funcItem->reimplemented || funcItem->alreadyFound || !funcItem->checked)
                    continue;

                const auto cmp = [funcItem](const Function *f) {
                    return f->name()->match(funcItem->function->name())
                                    && f->type().match(funcItem->function->type());
                };
                if (Utils::contains(insertedFunctions, cmp))
                    continue;
                insertedFunctions.append(funcItem->function);

                if (first) {
                    // Add comment
                    const QString comment = QLatin1String("\n// ") +
                            printer.prettyName(classItem->klass->name()) +
                            QLatin1String(" interface\n");
                    headerChangeSet.insert(m_insertPosDecl, comment);
                    first = false;
                }

                // Function type minimalization: As base class and derived class could be in
                // different namespaces, we must first make the type fully qualified before
                // it can get minimized.
                Clone cloner(control);
                Function newFunc(&cloner, nullptr, const_cast<Function *>(funcItem->function));
                newFunc.setEnclosingScope(const_cast<Class *>(targetClass));
                SubstitutionEnvironment envQualified;
                envQualified.setContext(context());
                envQualified.switchScope(classItem->klass->enclosingScope());
                UseQualifiedNames useQualifiedNames;
                envQualified.enter(&useQualifiedNames);
                newFunc.setReturnType(rewriteType(newFunc.returnType(), &envQualified, control));
                const int argc = newFunc.argumentCount();
                for (int i = 0; i < argc; ++i) {
                    Argument * const arg = newFunc.argumentAt(i)->asArgument();
                    QTC_ASSERT(arg, continue);
                    arg->setType(rewriteType(arg->type(), &envQualified, control));
                }
                SubstitutionEnvironment envMinimized;
                envMinimized.setContext(context());
                envMinimized.switchScope(targetClass->enclosingScope());
                envMinimized.enter(&useMinimalNames);
                const FullySpecifiedType tn = rewriteType(newFunc.type(), &envMinimized, control);
                QString declaration = printer.prettyType(tn, newFunc.unqualifiedName());

                if (m_factory->settings()->insertVirtualKeyword)
                    declaration = QLatin1String("virtual ") + declaration;
                if (m_factory->settings()->insertOverrideReplacement) {
                    const QString overrideReplacement = m_factory->settings()->overrideReplacement;
                    if (!overrideReplacement.isEmpty())
                        declaration += QLatin1Char(' ') + overrideReplacement;
                }
                if (m_factory->settings()->implementationMode
                        & InsertVirtualMethodsDialog::ModeInsideClass) {
                    declaration += QLatin1String("\n{\n}\n");
                } else {
                    declaration += QLatin1String(";\n");
                }

                const QString accessSpecString =
                        InsertionPointLocator::accessSpecToString(funcItem->accessSpec);
                if (accessSpecString != lastAccessSpecString) {
                    declaration = accessSpecString + QLatin1String(":\n") + declaration;
                    if (!lastAccessSpecString.isEmpty()) // separate if not direct after the comment
                        declaration = QLatin1String("\n") + declaration;
                    lastAccessSpecString = accessSpecString;
                }
                headerChangeSet.insert(m_insertPosDecl, declaration);

                // Insert definition outside class
                if (m_factory->settings()->implementationMode
                        & InsertVirtualMethodsDialog::ModeOutsideClass) {
                    const QString name = printer.prettyName(targetClass->name()) +
                            QLatin1String("::") + printer.prettyName(funcItem->function->name());
                    const QString defText = printer.prettyType(tn, name) + QLatin1String("\n{\n}");
                    headerChangeSet.insert(m_insertPosOutside,  QLatin1String("\n\n") + defText);
                }
            }
        }

        // Write header file
        if (!headerChangeSet.isEmpty()) {
            headerFile->setOpenEditor(true, m_insertPosDecl);
            headerFile->apply(headerChangeSet);
        }

        // Insert in implementation file
        if (m_factory->settings()->implementationMode
                & InsertVirtualMethodsDialog::ModeImplementationFile) {
            const Symbol *symbol = headerFile->cppDocument()->lastVisibleSymbolAt(
                        targetClass->line(), targetClass->column());
            if (!symbol)
                return;
            const Class *clazz = symbol->asClass();
            if (!clazz)
                return;

            CppRefactoringFilePtr implementationFile = refactoring.cppFile(m_cppFilePath);
            Utils::ChangeSet implementationChangeSet;
            const int insertPos = qMax(0, implementationFile->document()->characterCount() - 1);

            // make target lookup context
            Document::Ptr implementationDoc = implementationFile->cppDocument();
            int line, column;
            implementationDoc->translationUnit()->getPosition(insertPos, &line, &column);
            Scope *targetScope = implementationDoc->scopeAt(line, column);
            const LookupContext targetContext(implementationDoc, snapshot());
            ClassOrNamespace *targetCoN = targetContext.lookupType(targetScope);
            if (!targetCoN)
                targetCoN = targetContext.globalNamespace();

            // Loop through inserted declarations
            for (int i = targetClass->memberCount(); i < clazz->memberCount(); ++i) {
                Declaration *decl = clazz->memberAt(i)->asDeclaration();
                if (!decl)
                    continue;

                // setup rewriting to get minimally qualified names
                SubstitutionEnvironment env;
                env.setContext(context());
                env.switchScope(decl->enclosingScope());
                UseMinimalNames q(targetCoN);
                env.enter(&q);
                Control *control = context().bindings()->control().get();

                // rewrite the function type and name + create definition
                const FullySpecifiedType type = rewriteType(decl->type(), &env, control);
                const QString name = printer.prettyName(
                            LookupContext::minimalName(decl, targetCoN, control));
                const QString defText = printer.prettyType(type, name) + QLatin1String("\n{\n}");

                implementationChangeSet.insert(insertPos,  QLatin1String("\n\n") + defText);
            }

            implementationFile->apply(implementationChangeSet);
        }
    }

private:
    InsertVirtualMethodsDialog *m_factory = nullptr;
    const ClassSpecifierAST *m_classAST = nullptr;
    bool m_valid = false;
    FilePath m_cppFilePath;
    int m_insertPosDecl = 0;
    int m_insertPosOutside = 0;
    unsigned m_functionCount = 0;
};

class InsertVirtualMethodsFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    InsertVirtualMethodsFilterModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {}

    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

        // Handle base class
        if (!sourceParent.isValid()) {
            // check if any child is valid
            if (!sourceModel()->hasChildren(index))
                return false;
            if (!m_hideReimplemented)
                return true;

            for (int i = 0; i < sourceModel()->rowCount(index); ++i) {
                const QModelIndex child = sourceModel()->index(i, 0, index);
                if (!child.data(InsertVirtualMethodsDialog::Reimplemented).toBool())
                    return true;
            }
            return false;
        }

        if (m_hideReimplemented)
            return !index.data(InsertVirtualMethodsDialog::Reimplemented).toBool();
        return true;
    }

    bool hideReimplemented() const
    {
        return m_hideReimplemented;
    }

    void setHideReimplementedFunctions(bool show)
    {
        m_hideReimplemented = show;
        invalidateFilter();
    }

private:
    bool m_hideReimplemented = false;
};

// The tree of base classes and their virtual functions. The rows are the quick
// fix's own model, behind the proxy that hides the ones already reimplemented;
// the aspect only hands it over.
class VirtualFunctionsAspect final : public BaseAspect
{
public:
    VirtualFunctionsAspect(AspectContainer *container, QAbstractItemModel *model)
        : BaseAspect(container), m_model(model)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        p.filterPlaceholderText = Tr::tr("Filter");
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

private:
    QAbstractItemModel *const m_model;
};

// What the dialog asks. The values are read from and written back to
// VirtualMethodsSettings, which is what the quick fix itself looks at.
class InsertVirtualMethodsAspects final : public AspectContainer
{
public:
    InsertVirtualMethodsAspects(QAbstractItemModel *rows,
                                InsertVirtualMethodsFilterModel *filter);

    // The replacements the list offers: the two that are always there, plus
    // whatever has been typed into it before.
    void setAvailableReplacements(const QStringList &replacements);
    const QStringList &availableReplacements() const { return m_availableReplacements; }

    // The last entry is only offered where there is a file to put definitions
    // in, the way the widget dialog added and removed it.
    void setHasImplementationFile(bool hasFile);

    VirtualFunctionsAspect functions;
    BoolAspect hideReimplemented{this};
    SelectionAspect insertMode{this};
    BoolAspect virtualKeyword{this};
    BoolAspect useOverrideReplacement{this};
    StringSelectionAspect overrideReplacement{this};
    ActionAspect clearAddedReplacements{this};

private:
    InsertVirtualMethodsFilterModel *const m_filter;
    QStringList m_availableReplacements;
};

InsertVirtualMethodsAspects::InsertVirtualMethodsAspects(QAbstractItemModel *rows,
                                                         InsertVirtualMethodsFilterModel *filter)
    : functions(this, rows)
    , m_filter(filter)
{
    setAutoApply(true);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/CppEditor/InsertVirtualMethodsDialog.qml"));

    functions.setQmlName("Functions");

    hideReimplemented.setQmlName("HideReimplemented");
    hideReimplemented.setLabelText(Tr::tr("Hide reimplemented functions"));

    insertMode.setQmlName("InsertMode");
    insertMode.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

    virtualKeyword.setQmlName("VirtualKeyword");
    virtualKeyword.setLabelText(Tr::tr("Add \"virtual\" to function declaration"));

    useOverrideReplacement.setQmlName("UseOverrideReplacement");
    useOverrideReplacement.setLabelText(
        Tr::tr("Add \"override\" equivalent to function declaration:"));

    overrideReplacement.setQmlName("OverrideReplacement");
    overrideReplacement.setFillCallback(
        [this](const StringSelectionAspect::ResultCallback &cb) {
            QList<QStandardItem *> items;
            for (const QString &replacement : std::as_const(m_availableReplacements)) {
                auto item = new QStandardItem(replacement);
                // The list stores what it is called, not which row it is: it
                // is typed into as well as picked from.
                item->setData(replacement);
                items.append(item);
            }
            cb(items);
        });

    clearAddedReplacements.setQmlName("ClearAddedReplacements");
    clearAddedReplacements.setActionIcon(Utils::Icons::CLEAN_TOOLBAR.icon());
    clearAddedReplacements.setToolTip(Tr::tr("Clear Added \"override\" Equivalents"));
    clearAddedReplacements.setAction([this] {
        setAvailableReplacements(defaultOverrideReplacements());
    });

    // Only the replacement that is going to be inserted is worth choosing.
    connect(&useOverrideReplacement, &BaseAspect::changed, this, [this] {
        overrideReplacement.setEnabled(useOverrideReplacement.volatileValue());
    });
    connect(&hideReimplemented, &BaseAspect::changed, this, [this] {
        m_filter->setHideReimplementedFunctions(hideReimplemented.volatileValue());
    });
}

void InsertVirtualMethodsAspects::setAvailableReplacements(const QStringList &replacements)
{
    m_availableReplacements = replacements;
    overrideReplacement.ensureFilled();
    overrideReplacement.refill();
    clearAddedReplacements.setEnabled(
        m_availableReplacements.size() > defaultOverrideReplacements().size());
}

// What the dialog offers, from what was saved. Kept apart from where the
// settings are stored so that the two can be checked without a settings file.
void showSettings(const VirtualMethodsSettings &settings,
                  bool hasImplementationFile,
                  bool hasReimplementedFunctions,
                  InsertVirtualMethodsAspects *aspects)
{
    aspects->setAvailableReplacements(defaultOverrideReplacements()
                                      + settings.userAddedOverrideReplacements);

    aspects->hideReimplemented.setEnabled(hasReimplementedFunctions);
    aspects->hideReimplemented.setValue(settings.hideReimplementedFunctions);

    aspects->virtualKeyword.setValue(settings.insertVirtualKeyword);
    aspects->useOverrideReplacement.setValue(settings.insertOverrideReplacement);
    aspects->overrideReplacement.setEnabled(settings.insertOverrideReplacement);

    // No clamping: a StringSelectionAspect given an id its list does not hold
    // moves to the first entry by itself, which is what the widget dialog
    // clamped a stale row for.
    aspects->overrideReplacement.setValue(
        aspects->availableReplacements().value(settings.overrideReplacementIndex));

    aspects->setHasImplementationFile(hasImplementationFile);
    aspects->insertMode.setValue(
        aspects->insertMode.indexForItemValue(int(settings.implementationMode)));
}

// And the way back.
void readSettings(const InsertVirtualMethodsAspects &aspects, VirtualMethodsSettings *settings)
{
    settings->insertVirtualKeyword = aspects.virtualKeyword.value();
    settings->implementationMode = static_cast<InsertVirtualMethodsDialog::ImplementationMode>(
        aspects.insertMode.itemValue().toInt());
    settings->hideReimplementedFunctions = aspects.hideReimplemented.value();
    settings->insertOverrideReplacement = aspects.useOverrideReplacement.value();
    if (aspects.overrideReplacement.isEnabled())
        settings->overrideReplacement = aspects.overrideReplacement.value().trimmed();

    QSet<QString> addedReplacements = Utils::toSet(aspects.availableReplacements());
    addedReplacements.insert(settings->overrideReplacement);
    addedReplacements.subtract(Utils::toSet(defaultOverrideReplacements()));
    settings->userAddedOverrideReplacements
        = sortedAndTrimmedStringListWithoutEmptyElements(Utils::toList(addedReplacements));
    settings->overrideReplacementIndex = overrideReplacementRow(
        settings->overrideReplacement, settings->userAddedOverrideReplacements);
}

void InsertVirtualMethodsAspects::setHasImplementationFile(bool hasFile)
{
    insertMode.clearOptions();
    insertMode.addOption({Tr::tr("Insert only declarations"), {},
                          int(InsertVirtualMethodsDialog::ModeOnlyDeclarations)});
    insertMode.addOption({Tr::tr("Insert definitions inside class"), {},
                          int(InsertVirtualMethodsDialog::ModeInsideClass)});
    insertMode.addOption({Tr::tr("Insert definitions outside class"), {},
                          int(InsertVirtualMethodsDialog::ModeOutsideClass)});
    if (hasFile) {
        insertMode.addOption({Tr::tr("Insert definitions in implementation file"), {},
                              int(InsertVirtualMethodsDialog::ModeImplementationFile)});
    }
}

InsertVirtualMethodsDialog::InsertVirtualMethodsDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new VirtualMethodsSettings)
    , classFunctionModel(new InsertVirtualMethodsModel(this))
    , classFunctionFilterModel(new InsertVirtualMethodsFilterModel(this))
{
    classFunctionFilterModel->setSourceModel(classFunctionModel);
}

InsertVirtualMethodsDialog::~InsertVirtualMethodsDialog()
{
    delete m_settings;
}

void InsertVirtualMethodsDialog::initGui()
{
    if (m_aspects)
        return;

    setWindowTitle(Tr::tr("Insert Virtual Functions"));

    m_aspects.reset(new InsertVirtualMethodsAspects(
        classFunctionFilterModel,
        static_cast<InsertVirtualMethodsFilterModel *>(classFunctionFilterModel)));

    m_buttons = new QDialogButtonBox(this);
    m_buttons->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_aspects.get()));
    layout->addWidget(m_buttons);
}

void InsertVirtualMethodsDialog::initData()
{
    m_settings->read();
    showSettings(*m_settings, m_hasImplementationFile, m_hasReimplementedFunctions,
                 m_aspects.get());
    // The rows are a different set of classes every time, so nothing the
    // reader closed last time is about this tree.
    m_aspects->functions.expandControl();
    // A value the aspect already held is no change, so nothing would have told
    // the proxy what to hide.
    static_cast<InsertVirtualMethodsFilterModel *>(classFunctionFilterModel)
        ->setHideReimplementedFunctions(m_settings->hideReimplementedFunctions);
}

void InsertVirtualMethodsDialog::saveSettings()
{
    readSettings(*m_aspects, m_settings);
    m_settings->write();
}

const VirtualMethodsSettings *InsertVirtualMethodsDialog::settings() const
{
    return m_settings;
}

bool InsertVirtualMethodsDialog::gather()
{
    initGui();
    initData();
    m_aspects->functions.setFocusToInputField();

    // Expand the dialog a little bit
    adjustSize();
    resize(size() * 1.5);

    QPointer<InsertVirtualMethodsDialog> that(this);
    const int ret = exec();
    if (!that)
        return false;

    return (ret == QDialog::Accepted);
}

void InsertVirtualMethodsDialog::setHasImplementationFile(bool file)
{
    m_hasImplementationFile = file;
}

void InsertVirtualMethodsDialog::setHasReimplementedFunctions(bool functions)
{
    m_hasReimplementedFunctions = functions;
}

InsertVirtualMethods::InsertVirtualMethods(InsertVirtualMethodsDialog *dialog)
    : m_dialog(dialog)
{
    if (!dialog)
        m_dialog = new InsertVirtualMethodsDialog;
}

InsertVirtualMethods::~InsertVirtualMethods()
{
    m_dialog->deleteLater();
}

void InsertVirtualMethods::doMatch(const CppQuickFixInterface &interface,
                                   QuickFixOperations &result)
{
    QSharedPointer<InsertVirtualMethodsOp> op(new InsertVirtualMethodsOp(interface, m_dialog));
    if (op->isValid())
        result.append(op);
}

#ifdef WITH_TESTS

namespace Tests {

typedef QByteArray _;

/// Stub dialog of InsertVirtualMethodsDialog that does not pop up anything.
class InsertVirtualMethodsDialogTest : public InsertVirtualMethodsDialog
{
public:
    InsertVirtualMethodsDialogTest(ImplementationMode mode,
                                   bool insertVirtualKeyword,
                                   bool insertOverrideKeyword,
                                   QWidget *parent = 0)
        : InsertVirtualMethodsDialog(parent)
    {
        m_settings->implementationMode = mode;
        m_settings->insertVirtualKeyword = insertVirtualKeyword;
        m_settings->insertOverrideReplacement = insertOverrideKeyword;
        m_settings->overrideReplacement = QLatin1String("override");
    }

    bool gather() override { return true; }
    void saveSettings() override { }
};

class InsertVirtualMethodsTest : public QObject
{
    Q_OBJECT

private slots:
    void test_data();
    void test();
    void testImplementationFile();
    void testBaseClassInNamespace();
    void testTheDialogDrawsWithTheQmlItNames();
    void testTheModeListFollowsTheImplementationFile();
    void testOnlyAnInsertedReplacementIsWorthChoosing();
    void testClearingIsOfferedOnlyForWhatWasAdded();
    void testHidingReimplementedFunctionsFiltersTheTree();
    void testTheSavedRowPointsIntoTheListItIsReadFrom();
    void testTheAnswersMakeTheRoundTripThroughTheSettings();
};

// A tree of one class with the functions named, the ones in reimplemented
// marked as already found. Enough for what the dialog does with the model;
// the symbols are the quick fix's business, not the dialog's.
static ClassItem *classWithFunctions(const QString &name,
                                     const QStringList &functions,
                                     const QStringList &reimplemented)
{
    auto classItem = new ClassItem(name, nullptr);
    for (const QString &function : functions) {
        auto item = new FunctionItem(nullptr, function, classItem);
        item->alreadyFound = reimplemented.contains(function);
        item->row = classItem->functions.count();
        classItem->functions.append(item);
    }
    return classItem;
}

void InsertVirtualMethodsTest::test_data()
{
    QTest::addColumn<InsertVirtualMethodsDialog::ImplementationMode>("implementationMode");
    QTest::addColumn<bool>("insertVirtualKeyword");
    QTest::addColumn<bool>("insertOverrideKeyword");
    QTest::addColumn<QByteArray>("original");
    QTest::addColumn<QByteArray>("expected");

    // Check: Insert only declarations
    QTest::newRow("onlyDecl")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "};\n"
    );

    // Check: Insert only declarations without virtual keyword but with override
    QTest::newRow("onlyDeclWithoutVirtual")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << false << true << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    int virtualFuncA() override;\n"
        "};\n"
    );

    // Check: Are access specifiers considered
    QTest::newRow("Access")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "protected:\n"
        "    virtual int b() = 0;\n"
        "private:\n"
        "    virtual int c() = 0;\n"
        "public slots:\n"
        "    virtual int d() = 0;\n"
        "protected slots:\n"
        "    virtual int e() = 0;\n"
        "private slots:\n"
        "    virtual int f() = 0;\n"
        "signals:\n"
        "    virtual int g() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "protected:\n"
        "    virtual int b() = 0;\n"
        "private:\n"
        "    virtual int c() = 0;\n"
        "public slots:\n"
        "    virtual int d() = 0;\n"
        "protected slots:\n"
        "    virtual int e() = 0;\n"
        "private slots:\n"
        "    virtual int f() = 0;\n"
        "signals:\n"
        "    virtual int g() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n\n"
        "protected:\n"
        "    virtual int b();\n\n"
        "private:\n"
        "    virtual int c();\n\n"
        "public slots:\n"
        "    virtual int d();\n\n"
        "protected slots:\n"
        "    virtual int e();\n\n"
        "private slots:\n"
        "    virtual int f();\n\n"
        "signals:\n"
        "    virtual int g();\n"
        "};\n"
    );

    // Check: Is a base class of a base class considered.
    QTest::newRow("Superclass")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n"
        "\n"
        "    // BaseB interface\n"
        "public:\n"
        "    virtual int b();\n"
        "};\n"
    );


    // Check: Do not insert reimplemented functions twice.
    QTest::newRow("SuperclassOverride")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n"
        "};\n"
    );

    // Check: Insert only declarations for pure virtual function
    QTest::newRow("PureVirtualOnlyDecl")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "};\n"
    );

    // Check: Insert pure virtual functions inside class
    QTest::newRow("PureVirtualInside")
        << InsertVirtualMethodsDialog::ModeInsideClass << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA()\n"
        "    {\n"
        "    }\n"
        "};\n"
    );

    // Check: Overloads
    QTest::newRow("Overloads")
        << InsertVirtualMethodsDialog::ModeInsideClass << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virt(int i) = 0;\n"
        "    virtual int virt(double d) = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virt(int i) = 0;\n"
        "    virtual int virt(double d) = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virt(int i)\n"
        "    {\n"
        "    }\n"
        "    virtual int virt(double d)\n"
        "    {\n"
        "    }\n"
        "};\n"
    );

    // Check: Insert inside class
    QTest::newRow("inside")
        << InsertVirtualMethodsDialog::ModeInsideClass << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA()\n"
        "    {\n"
        "    }\n"
        "};\n"
    );

    // Check: Insert outside class
    QTest::newRow("outside")
        << InsertVirtualMethodsDialog::ModeOutsideClass << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "};\n\n"
        "int Derived::virtualFuncA()\n"
        "{\n"
        "}\n"
    );

    // Check: No trigger: all implemented
    QTest::newRow("notrigger_allImplemented")
        << InsertVirtualMethodsDialog::ModeOutsideClass << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "    virtual operator==(const BaseA &);\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "    virtual operator==(const BaseA &);\n"
        "};\n"
        ) << _();

    // Check: One pure, one not
    QTest::newRow("Some_Pure")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "    virtual int virtualFuncB();\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "    virtual int virtualFuncB();\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "};\n"
    );

    // Check: Pure function in derived class
    QTest::newRow("Pure_in_Derived")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n"
        "};\n"
    );

    // Check: One pure function in base class, one in derived
    QTest::newRow("Pure_in_Base_And_Derived")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n"
        "    virtual int b();\n"
        "};\n"
    );

    // Check: One pure function in base class, two in derived
    QTest::newRow("Pure_in_Base_And_Derived_2")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "    virtual int c() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "    virtual int c() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n"
        "    virtual int b();\n"
        "\n"
        "    // BaseB interface\n"
        "public:\n"
        "    virtual int c();\n"
        "};\n"
    );

    // Check: Remove final function
    QTest::newRow("final_function_removed")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() final = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() final = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int b();\n"
        "};\n"
    );

    // Check: Remove multiple final functions
    QTest::newRow("multiple_final_functions_removed")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int c() = 0;\n"
        "};\n\n"
        "class BaseC : public BaseB {\n"
        "public:\n"
        "    virtual int a() final = 0;\n"
        "    virtual int d() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseC {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int c() = 0;\n"
        "};\n\n"
        "class BaseC : public BaseB {\n"
        "public:\n"
        "    virtual int a() final = 0;\n"
        "    virtual int d() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseC {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int b();\n"
        "\n"
        "    // BaseB interface\n"
        "public:\n"
        "    virtual int c();\n"
        "\n"
        "    // BaseC interface\n"
        "public:\n"
        "    virtual int d();\n"
        "};\n"
    );

    // Check: Insert multiply-inherited virtual function only once.
    QTest::newRow("multiple_inheritance_insert")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << false << true << _(
        "struct Base1 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Base2 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct @Derived : Base1, Base2 {\n"
        "};\n") << _(
        "struct Base1 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Base2 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Derived : Base1, Base2 {\n\n"
        "    // Base2 interface\n"
        "public:\n"
        "    void virt() override;\n"
        "};\n");

    // Check: Do not insert multiply-inherited virtual function that has been re-implemented
    //        along the way.
    QTest::newRow("multiple_inheritance_no_insert")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << false << true << _(
        "struct Base1 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Base2 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Derived1 : Base1, Base2 {\n"
        "    void virt() override;\n"
        "};\n\n"
        "struct @Derived2 : Derived1\n"
        "};\n") << _(
        "struct Base1 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Base2 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Derived1 : Base1, Base2 {\n"
        "    void virt() override;\n"
        "};\n\n"
        "struct Derived2 : Derived1\n"
        "};\n");

    QTest::newRow("base class via using")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << false << true
        << _(R"cpp(
                  namespace A { struct Base { virtual void foo() = 0; }; } // namespace A
                  using A::Base;
                  struct @Derived : Base {};)cpp")
        << _(R"cpp(
                  namespace A { struct Base { virtual void foo() = 0; }; } // namespace A
                  using A::Base;
                  struct @Derived : Base {
                      // Base interface
                  public:
                      void foo() override;
                  };)cpp");
}

void InsertVirtualMethodsTest::test()
{
    QFETCH(InsertVirtualMethodsDialog::ImplementationMode, implementationMode);
    QFETCH(bool, insertVirtualKeyword);
    QFETCH(bool, insertOverrideKeyword);
    QFETCH(QByteArray, original);
    QFETCH(QByteArray, expected);

    InsertVirtualMethods factory(
                new Tests::InsertVirtualMethodsDialogTest(implementationMode,
                                                          insertVirtualKeyword,
                                                          insertOverrideKeyword));
    Tests::QuickFixOperationTest(Tests::singleDocument(original, expected), &factory);
}

/// Check: Insert in implementation file
void InsertVirtualMethodsTest::testImplementationFile()
{
    QList<Tests::TestDocumentPtr> testFiles;
    QByteArray original;
    QByteArray expected;

    // Header File
    original =
        "class BaseA {\n"
        "public:\n"
        "    virtual int a(const std::vector<int> &v) = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "public:\n"
        "    Derived();\n"
        "};\n";
    expected =
        "class BaseA {\n"
        "public:\n"
        "    virtual int a(const std::vector<int> &v) = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "public:\n"
        "    Derived();\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a(const std::vector<int> &v);\n"
        "};\n";
    testFiles << Tests::CppTestDocument::create("file.h", original, expected);

    // Source File
    original = "#include \"file.h\"\n";
    expected =
        "#include \"file.h\"\n"
        "\n\n"
        "int Derived::a(const std::vector<int> &v)\n"
        "{\n}";
    testFiles << Tests::CppTestDocument::create("file.cpp", original, expected);

    InsertVirtualMethods factory(new Tests::InsertVirtualMethodsDialogTest(
                                     InsertVirtualMethodsDialog::ModeImplementationFile,
                                     true,
                                     false));
    Tests::QuickFixOperationTest(testFiles, &factory);
}

/// Check: Qualified names.
void InsertVirtualMethodsTest::testBaseClassInNamespace()
{
    QList<Tests::TestDocumentPtr> testFiles;
    QByteArray original;
    QByteArray expected;

    // Header File
    original =
        "namespace BaseNS {enum BaseEnum {EnumA = 1};}\n"
        "namespace BaseNS {\n"
        "class Base {\n"
        "public:\n"
        "    virtual BaseEnum a(BaseEnum e) = 0;\n"
        "};\n"
        "}\n"
        "class Deri@ved : public BaseNS::Base {\n"
        "public:\n"
        "    Derived();\n"
        "};\n";
    expected =
        "namespace BaseNS {enum BaseEnum {EnumA = 1};}\n"
        "namespace BaseNS {\n"
        "class Base {\n"
        "public:\n"
        "    virtual BaseEnum a(BaseEnum e) = 0;\n"
        "};\n"
        "}\n"
        "class Deri@ved : public BaseNS::Base {\n"
        "public:\n"
        "    Derived();\n"
        "\n"
        "    // Base interface\n"
        "public:\n"
        "    virtual BaseNS::BaseEnum a(BaseNS::BaseEnum e);\n"
        "};\n";
    testFiles << Tests::CppTestDocument::create("file.h", original, expected);

    // Source File
    original = "#include \"file.h\"\n";
    expected =
        "#include \"file.h\"\n"
        "\n\n"
        "BaseNS::BaseEnum Derived::a(BaseNS::BaseEnum e)\n"
        "{\n}";
    testFiles << Tests::CppTestDocument::create("file.cpp", original, expected);

    InsertVirtualMethods factory(new Tests::InsertVirtualMethodsDialogTest(
                                     InsertVirtualMethodsDialog::ModeImplementationFile,
                                     true,
                                     false));
    Tests::QuickFixOperationTest(testFiles, &factory);
}

void InsertVirtualMethodsTest::testTheDialogDrawsWithTheQmlItNames()
{
    InsertVirtualMethodsModel rows;
    InsertVirtualMethodsFilterModel filter;
    filter.setSourceModel(&rows);
    InsertVirtualMethodsAspects aspects(&filter, &filter);

    const Result<> rendered
        = Core::aspectFormRenders(&aspects, "InsertVirtualMethodsDialog.qml");
    QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
}

void InsertVirtualMethodsTest::testTheModeListFollowsTheImplementationFile()
{
    InsertVirtualMethodsModel rows;
    InsertVirtualMethodsFilterModel filter;
    filter.setSourceModel(&rows);
    InsertVirtualMethodsAspects aspects(&filter, &filter);

    aspects.setHasImplementationFile(false);
    QCOMPARE(aspects.insertMode.optionCount(), 3);
    QVERIFY2(aspects.insertMode.indexForItemValue(
                 int(InsertVirtualMethodsDialog::ModeImplementationFile)) < 0,
             "definitions were offered a file to go in that is not there");

    aspects.setHasImplementationFile(true);
    QCOMPARE(aspects.insertMode.optionCount(), 4);
    QCOMPARE(aspects.insertMode.itemValueForIndex(3).toInt(),
             int(InsertVirtualMethodsDialog::ModeImplementationFile));

    // And asking again drops it, rather than adding a second one: the dialog
    // is reused, and the widget it replaces removed the entry.
    aspects.setHasImplementationFile(false);
    QCOMPARE(aspects.insertMode.optionCount(), 3);
}

void InsertVirtualMethodsTest::testOnlyAnInsertedReplacementIsWorthChoosing()
{
    InsertVirtualMethodsModel rows;
    InsertVirtualMethodsFilterModel filter;
    filter.setSourceModel(&rows);
    InsertVirtualMethodsAspects aspects(&filter, &filter);

    aspects.useOverrideReplacement.setValue(true);
    QVERIFY(aspects.overrideReplacement.isEnabled());
    aspects.useOverrideReplacement.setValue(false);
    QVERIFY2(!aspects.overrideReplacement.isEnabled(),
             "a replacement can be chosen that is not going to be written");
}

void InsertVirtualMethodsTest::testClearingIsOfferedOnlyForWhatWasAdded()
{
    InsertVirtualMethodsModel rows;
    InsertVirtualMethodsFilterModel filter;
    filter.setSourceModel(&rows);
    InsertVirtualMethodsAspects aspects(&filter, &filter);

    aspects.setAvailableReplacements(defaultOverrideReplacements());
    QVERIFY2(!aspects.clearAddedReplacements.isEnabled(),
             "there was something to clear with nothing added");

    aspects.setAvailableReplacements(defaultOverrideReplacements() << "OVERRIDE");
    QVERIFY(aspects.clearAddedReplacements.isEnabled());
    QCOMPARE(aspects.overrideReplacement.presentation().choices.size(), 3);

    // The button drops what was added, and says so by going quiet.
    aspects.clearAddedReplacements.triggerAction();
    QCOMPARE(aspects.availableReplacements(), defaultOverrideReplacements());
    QVERIFY(!aspects.clearAddedReplacements.isEnabled());
    QCOMPARE(aspects.overrideReplacement.presentation().choices.size(), 2);
}

void InsertVirtualMethodsTest::testHidingReimplementedFunctionsFiltersTheTree()
{
    InsertVirtualMethodsModel rows;
    rows.addClass(classWithFunctions("Base", {"one", "two"}, {"two"}));
    rows.addClass(classWithFunctions("AllDone", {"three"}, {"three"}));
    InsertVirtualMethodsFilterModel filter;
    filter.setSourceModel(&rows);
    InsertVirtualMethodsAspects aspects(&filter, &filter);

    QCOMPARE(filter.rowCount(QModelIndex()), 2);
    QCOMPARE(filter.rowCount(filter.index(0, 0)), 2);

    aspects.hideReimplemented.setValue(true);
    // The class with nothing left to insert goes with its functions.
    QCOMPARE(filter.rowCount(QModelIndex()), 1);
    QCOMPARE(filter.rowCount(filter.index(0, 0)), 1);

    aspects.hideReimplemented.setValue(false);
    QCOMPARE(filter.rowCount(QModelIndex()), 2);
}

void InsertVirtualMethodsTest::testTheSavedRowPointsIntoTheListItIsReadFrom()
{
    // Nothing added: the row is the row in the defaults.
    QCOMPARE(overrideReplacementRow("Q_DECL_OVERRIDE", {}), 1);
    // Something added: the list read back is the defaults plus what was
    // added, sorted, so the row has to be found in that.
    QCOMPARE(overrideReplacementRow("OVERRIDE", {"OVERRIDE", "ZZZ"}), 2);
    QCOMPARE(overrideReplacementRow("ZZZ", {"OVERRIDE", "ZZZ"}), 3);
    // A replacement that is in neither opens the list on its first entry
    // rather than on nothing.
    QCOMPARE(overrideReplacementRow("gone", {}), 0);
}

void InsertVirtualMethodsTest::testTheAnswersMakeTheRoundTripThroughTheSettings()
{
    InsertVirtualMethodsModel rows;
    InsertVirtualMethodsFilterModel filter;
    filter.setSourceModel(&rows);
    InsertVirtualMethodsAspects aspects(&filter, &filter);

    VirtualMethodsSettings settings;
    settings.insertVirtualKeyword = true;
    settings.hideReimplementedFunctions = true;
    settings.insertOverrideReplacement = true;
    settings.implementationMode = InsertVirtualMethodsDialog::ModeOutsideClass;
    settings.userAddedOverrideReplacements = {"OVERRIDE"};
    settings.overrideReplacementIndex = 2;

    showSettings(settings, true, true, &aspects);
    QVERIFY(aspects.virtualKeyword.value());
    QVERIFY(aspects.hideReimplemented.value());
    QVERIFY(aspects.hideReimplemented.isEnabled());
    QVERIFY(aspects.useOverrideReplacement.value());
    QVERIFY(aspects.overrideReplacement.isEnabled());
    QCOMPARE(aspects.overrideReplacement.value(), QString("OVERRIDE"));
    QCOMPARE(aspects.insertMode.itemValue().toInt(),
             int(InsertVirtualMethodsDialog::ModeOutsideClass));

    VirtualMethodsSettings written;
    readSettings(aspects, &written);
    QCOMPARE(written.insertVirtualKeyword, true);
    QCOMPARE(written.hideReimplementedFunctions, true);
    QCOMPARE(written.insertOverrideReplacement, true);
    QCOMPARE(written.implementationMode, InsertVirtualMethodsDialog::ModeOutsideClass);
    QCOMPARE(written.overrideReplacement, QString("OVERRIDE"));
    QCOMPARE(written.userAddedOverrideReplacements, QStringList{"OVERRIDE"});
    QCOMPARE(written.overrideReplacementIndex, 2);

    // A row the list is too short for opens it on its first entry. The aspect
    // does that itself for an id it cannot find, so this pins its behaviour
    // rather than the dialog's.
    settings.overrideReplacementIndex = 7;
    showSettings(settings, true, true, &aspects);
    QCOMPARE(aspects.overrideReplacement.value(), defaultOverrideReplacements().first());

    // Nothing is written for a replacement that is not going to be inserted:
    // the last one chosen stays what it was.
    aspects.useOverrideReplacement.setValue(false);
    VirtualMethodsSettings unchanged;
    unchanged.overrideReplacement = "kept";
    readSettings(aspects, &unchanged);
    QCOMPARE(unchanged.overrideReplacement, QString("kept"));
    QCOMPARE(unchanged.insertOverrideReplacement, false);

    // Only the entry that is offered where there is a file to put definitions
    // in comes back when there is one.
    showSettings(settings, false, false, &aspects);
    QCOMPARE(aspects.insertMode.optionCount(), 3);
    QVERIFY2(!aspects.hideReimplemented.isEnabled(),
             "reimplemented functions could be hidden with none to hide");
}

} // namespace Tests

InsertVirtualMethods *InsertVirtualMethods::createTestFactory()
{
    return new InsertVirtualMethods(new Tests::InsertVirtualMethodsDialogTest(
                                        InsertVirtualMethodsDialog::ModeOutsideClass, true, false));
}

QObject *InsertVirtualMethods::createTest()
{
    return new Tests::InsertVirtualMethodsTest;
}

#endif // WITH_TESTS

} // namespace CppEditor::Internal

#include "cppinsertvirtualmethods.moc"

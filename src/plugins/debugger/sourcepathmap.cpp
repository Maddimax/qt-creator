// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "sourcepathmap.h"

#include "commonoptionspage.h"
#include "debuggerengine.h"
#include "debuggertr.h"

#include <utils/aspectpresentation.h>
#include <utils/dirtysettings.h>
#include <qtsupport/baseqtversion.h>
#include <utils/elfreader.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/macroexpander.h>
#include <utils/qtcassert.h>
#include <utils/qtcsettings.h>

#include <QAbstractTableModel>
#include <QDir>

using namespace Utils;

namespace Debugger::Internal {

enum { SourceColumn, TargetColumn, ColumnCount };

// Qt's various build paths for unpatched versions.
static QStringList qtBuildPaths()
{
    if (HostOsInfo::isWindowsHost()) {
        return {"Q:/qt5_workdir/w/s",
                "C:/work/build/qt5_workdir/w/s",
                "c:/users/qt/work/qt",
                "c:/Users/qt/work/install",
                "/Users/qt/work/qt"};
    } else if (HostOsInfo::isMacHost()) {
        return { "/Users/qt/work/qt" };
    } else {
        return { "/home/qt/work/qt" };
    }
}

/*!
    \class Debugger::Internal::SourcePathMappingModel

    \brief Rows of source and target path, for editing.

    The aspect's value is a QMap keyed by source path, which cannot hold a row
    that is half typed - two rows with no source yet are one entry, and an
    unfinished row would come and go as the key changes. So the rows are kept
    here and the map is derived from them, leaving out any row that is not
    filled in on both sides. That is also what the old widget did, except it
    kept the rows in itself and recognized an unfinished one by the placeholder
    text it had written into it.
*/
class SourcePathMappingModel : public QAbstractTableModel
{
public:
    using QAbstractTableModel::QAbstractTableModel;

    using Row = std::pair<QString, QString>;

    SourcePathMap sourcePathMap() const
    {
        SourcePathMap map;
        for (const Row &row : m_rows) {
            if (!row.first.isEmpty() && !row.second.isEmpty())
                map.insert(row.first, row.second);
        }
        return map;
    }

    void setSourcePathMap(const SourcePathMap &map)
    {
        beginResetModel();
        m_rows.clear();
        for (auto it = map.cbegin(), end = map.cend(); it != end; ++it)
            m_rows.append({it.key(), QDir::toNativeSeparators(it.value())});
        endResetModel();
    }

    void addMappings(const QStringList &sources, const QString &target)
    {
        if (sources.isEmpty())
            return;
        beginInsertRows({}, m_rows.size(), m_rows.size() + sources.size() - 1);
        for (const QString &source : sources)
            m_rows.append({source, QDir::toNativeSeparators(target)});
        endInsertRows();
        markSettingsDirty();
    }

    int rowCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    int columnCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return index.column() == SourceColumn ? m_rows.at(index.row()).first
                                                  : m_rows.at(index.row()).second;
        case AspectTable::EditableRole:
            return AspectTable::isWritable(flags(index));
        default:
            return {};
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (role != Qt::EditRole)
            return false;
        Row &row = m_rows[index.row()];
        if (index.column() == SourceColumn)
            row.first = QDir::cleanPath(value.toString().trimmed());
        else
            row.second = value.toString();
        emit dataChanged(index, index);
        markSettingsDirty();
        return true;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Vertical || role != Qt::DisplayRole)
            return {};
        return section == SourceColumn ? Tr::tr("Source path") : Tr::tr("Target path");
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;
    }

    bool insertRows(int row, int count, const QModelIndex &parent) override
    {
        if (parent.isValid())
            return false;
        beginInsertRows(parent, row, row + count - 1);
        m_rows.insert(row, count, {});
        endInsertRows();
        markSettingsDirty();
        return true;
    }

    bool removeRows(int row, int count, const QModelIndex &parent) override
    {
        if (parent.isValid())
            return false;
        beginRemoveRows(parent, row, row + count - 1);
        m_rows.remove(row, count);
        endRemoveRows();
        markSettingsDirty();
        return true;
    }

private:
    QList<Row> m_rows;
};

//
// SourcePathMapAspect
//

class SourcePathMapAspectPrivate
{
public:
    explicit SourcePathMapAspectPrivate(QObject *parent)
        : m_model(parent)
    {}

    SourcePathMappingModel m_model;
};

SourcePathMapAspect::SourcePathMapAspect(AspectContainer *container)
    : TypedAspect(container), d(new SourcePathMapAspectPrivate(this))
{
    setQmlName("SourcePathMap");
    setLabelText(Tr::tr("Source paths mapping:"));
    setToolTip(Tr::tr("<p>Mappings of source file folders to "
                  "be used in the debugger can be entered here.</p>"
                  "<p>This is useful when using a copy of the source tree "
                  "at a location different from the one "
                  "at which the modules where built, for example, while "
                  "doing remote debugging.</p>"
                  "<p>If source is specified as a regular expression by starting it with an "
                  "open parenthesis, the paths in the ELF are matched with the "
                  "regular expression to automatically determine the source path.</p>"
                  "<p>Example: <b>(/home/.*/Project)/KnownSubDir -> D:\\Project</b> will "
                  "substitute ELF built by any user to your local project directory."));

    // The model is what the reader edits, so a change in it is a change in the
    // volatile value. BaseAspect::apply() checks that invariant and barks when
    // it does not hold - pulling only when someone asks left the value stale
    // between an edit and an apply.
    const auto modelEdited = [this] {
        if (guiToVolatileValue()) {
            Utils::checkSettingsDirty();
            emit volatileValueChanged();
        }
    };
    connect(&d->m_model, &QAbstractItemModel::dataChanged, this, modelEdited);
    connect(&d->m_model, &QAbstractItemModel::rowsInserted, this, modelEdited);
    connect(&d->m_model, &QAbstractItemModel::rowsRemoved, this, modelEdited);
    connect(&d->m_model, &QAbstractItemModel::modelReset, this, modelEdited);
}

SourcePathMapAspect::~SourcePathMapAspect()
{
    delete d;
}

void SourcePathMapAspect::fromMap(const Store &)
{
    QTC_CHECK(false); // This is only used via read/writeSettings
}

void SourcePathMapAspect::toMap(Store &) const
{
    QTC_CHECK(false);
}

AspectPresentation SourcePathMapAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::Table;
    return p;
}

QAbstractItemModel *SourcePathMapAspect::tableModel()
{
    return &d->m_model;
}

bool SourcePathMapAspect::hasQtBuildPaths()
{
    return !qtBuildPaths().isEmpty();
}

void SourcePathMapAspect::addQtSources(const FilePath &qtSourcesPath)
{
    // An unpatched Qt reports the path it was built at, which is one of a known
    // handful, so every one of them maps to the sources the user just picked.
    d->m_model.addMappings(qtBuildPaths(), qtSourcesPath.toUrlishString());
}

bool SourcePathMapAspect::isDirty() const
{
    const_cast<SourcePathMapAspect *>(this)->guiToVolatileValue();
    return m_value != m_volatileValue;
}

bool SourcePathMapAspect::guiToVolatileValue()
{
    const SourcePathMap old = m_volatileValue;
    m_volatileValue = d->m_model.sourcePathMap();
    return m_volatileValue != old;
}

void SourcePathMapAspect::volatileValueToGui()
{
    d->m_model.setSourcePathMap(m_volatileValue);
}

const char sourcePathMappingArrayNameC[] = "SourcePathMappings";
const char sourcePathMappingSourceKeyC[] = "Source";
const char sourcePathMappingTargetKeyC[] = "Target";

void SourcePathMapAspect::writeSettings() const
{
    const SourcePathMap sourcePathMap = value();
    QtcSettings &s = userSettings();
    s.beginWriteArray(sourcePathMappingArrayNameC);
    if (!sourcePathMap.isEmpty()) {
        const Key sourcePathMappingSourceKey(sourcePathMappingSourceKeyC);
        const Key sourcePathMappingTargetKey(sourcePathMappingTargetKeyC);
        int i = 0;
        for (auto it = sourcePathMap.constBegin(), cend = sourcePathMap.constEnd();
             it != cend;
             ++it, ++i) {
            s.setArrayIndex(i);
            s.setValue(sourcePathMappingSourceKey, it.key());
            s.setValue(sourcePathMappingTargetKey, it.value());
        }
    }
    s.endArray();
}

void SourcePathMapAspect::readSettings()
{
    QtcSettings &s = userSettings();
    SourcePathMap sourcePathMap;
    if (const int count = s.beginReadArray(sourcePathMappingArrayNameC)) {
        const Key sourcePathMappingSourceKey(sourcePathMappingSourceKeyC);
        const Key sourcePathMappingTargetKey(sourcePathMappingTargetKeyC);
        for (int i = 0; i < count; ++i) {
             s.setArrayIndex(i);
             const QString key = s.value(sourcePathMappingSourceKey).toString();
             const QString value = s.value(sourcePathMappingTargetKey).toString();
             sourcePathMap.insert(key, value);
        }
    }
    s.endArray();
    setValue(sourcePathMap);
    d->m_model.setSourcePathMap(sourcePathMap);
}

static bool hasQtSources(const FilePath &qtSourceLocation)
{
    static const QString qglobal = "qtbase/src/corelib/global/qglobal.h";
    return (qtSourceLocation / qglobal).exists();
}

QStringList qtBuildSourceRoots(const QByteArray &debugStrings)
{
    static const QByteArray marker = "/qtbase/src/";
    QStringList roots;
    for (qsizetype hit = debugStrings.indexOf(marker); hit >= 0;
         hit = debugStrings.indexOf(marker, hit + marker.size())) {
        // The recorded path is the NUL terminated string around the marker, its
        // root everything before the marker. Only absolute roots can be mapped.
        const qsizetype start = debugStrings.lastIndexOf('\0', hit) + 1;
        if (hit <= start || debugStrings.at(start) != '/')
            continue;
        const QString root = QString::fromUtf8(debugStrings.constData() + start, hit - start);
        if (!roots.contains(root))
            roots.append(root);
    }
    return roots;
}

// Source paths live in .debug_str (DWARF <= 4) or .debug_line_str (DWARF 5).
static QStringList sourceRootsFromSections(ElfReader &reader, const FilePath &binary)
{
    static const QByteArray sections[] = {".debug_str", ".debug_line_str"};
    const ElfData elfData = reader.readHeaders();
    QStringList roots;
    for (const QByteArray &section : sections) {
        if (elfData.indexOf(section) == -1)
            continue;
        const std::unique_ptr<ElfMapper> mapper = reader.readSection(section);
        if (!mapper) {
            qWarning() << "Cannot read" << section << "of" << binary << ":"
                       << reader.errorString();
            continue;
        }
        roots += qtBuildSourceRoots(
            QByteArray::fromRawData(mapper->start, qsizetype(mapper->fdlen)));
    }
    roots.removeDuplicates();
    return roots;
}

// An installed Qt library usually carries no debug information itself, only a
// .gnu_debuglink or a build id naming a companion file. The candidates and their
// order are gdb's, minus the checksum in the debug link.
FilePath debugInfoFile(const FilePath &library, const QByteArray &debugLink,
                       const QByteArray &buildId, const FilePath &debugInfoDir)
{
    const FilePath dir = library.parentDir();
    FilePaths candidates;
    // The build id comes first because it identifies the companion exactly,
    // where the debug link is only a name. Distributions ship the companion in
    // the build id tree and name it without a directory in the debug link, so
    // this is also the only candidate that finds it there.
    if (buildId.size() > 2 && !debugInfoDir.isEmpty()) {
        const QString id = QString::fromLatin1(buildId);
        candidates << debugInfoDir / ".build-id" / id.left(2) / (id.mid(2) + ".debug");
    }
    if (!debugLink.isEmpty()) {
        const QString name = QString::fromUtf8(debugLink);
        candidates << dir / name << dir / ".debug" / name;
        if (!debugInfoDir.isEmpty())
            candidates << debugInfoDir.pathAppended(dir.path()) / name;
    }

    for (const FilePath &candidate : candidates) {
        if (candidate != library && candidate.isReadableFile())
            return candidate;
    }
    return library;
}

static QStringList sourceRootsFromLibrary(const FilePath &library, const FilePath &debugInfoDir)
{
    ElfReader reader(library);
    const ElfData elfData = reader.readHeaders();
    const FilePath companion = debugInfoFile(library, elfData.debugLink, elfData.buildId,
                                             debugInfoDir);
    if (companion == library)
        return sourceRootsFromSections(reader, library);

    ElfReader companionReader(companion);
    return sourceRootsFromSections(companionReader, companion);
}

// gdb's own debug-file-directory default does not depend on the
// autoEnrichParameters() setting that fills debugInfoLocation() in, so the
// companion of a distro-packaged Qt has to be looked for either way.
FilePath debugInfoDirectory(const DebuggerRunParameters &sp)
{
    const FilePath location = sp.debugInfoLocation();
    return location.isEmpty() ? sp.sysRoot() / "/usr/lib/debug" : location;
}

QStringList qtBuildSourceRoots(const DebuggerRunParameters &sp, const QtSupport::QtVersion *qt)
{
    if (!qt || sp.toolChainAbi().binaryFormat() != ProjectExplorer::Abi::ElfFormat)
        return {};
    if (!hasQtSources(sp.qtSourceLocation()))
        return {};

    const FilePath library = qt->libraryPath()
        / QString("libQt%1Core.so.%1").arg(qt->qtVersion().majorVersion());
    if (!library.isLocal())
        return {};

    const FilePath debugInfoDir = debugInfoDirectory(sp);
    // The library is local, so a companion on another device is of no use.
    if (!debugInfoDir.isLocal())
        return {};

    return sourceRootsFromLibrary(library, debugInfoDir);
}

/* Merge settings for an installed Qt (unless another setting is already in the map. */
SourcePathMap mergePlatformQtPath(const QString &qtSourceLocation,
                                  const QStringList &qtBuildSourceRoots,
                                  const SourcePathMap &in)
{
    SourcePathMap rc = in;
    // The root recorded in the debug information covers builds whose path is
    // not one of the qtBuildPaths() guesses.
    for (const QString &buildRoot : qtBuildSourceRoots) {
        if (buildRoot != qtSourceLocation && !rc.contains(buildRoot))
            rc.insert(buildRoot, qtSourceLocation);
    }
    for (const QString &buildPath : qtBuildPaths()) {
        if (!rc.contains(buildPath)) // Do not overwrite user settings.
            rc.insert(buildPath, qtSourceLocation);
    }
    return rc;
}

SourcePathMap mergePlatformQtPath(const DebuggerRunParameters &sp, const SourcePathMap &in)
{
    const FilePath sourceLocation = sp.qtSourceLocation();
    if (!hasQtSources(sourceLocation))
        return in;

    return mergePlatformQtPath(sourceLocation.path(), sp.qtBuildSourceRoots(), in);
}

SourcePathMap mergeStartParametersSourcePathMap(const DebuggerRunParameters &sp,
                                                const SourcePathMap &in)
{
    // Do not overwrite user settings.
    SourcePathMap rc = sp.sourcePathMap();
    for (auto it = in.constBegin(), end = in.constEnd(); it != end; ++it) {
        // Entries that start with parenthesis are handled in
        // DebuggerEngine::validateRunParameters
        if (!it.key().startsWith('('))
            rc.insert(it.key(), sp.macroExpander()->expand(it.value()));
    }
    return rc;
}

} // Debugger::Internal

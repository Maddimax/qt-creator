// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <utils/filepath.h>
#include <utils/id.h>

#include <QObject>

namespace Utils {
class InfoBar;
class MinimizableInfoBars;
} // namespace Utils

namespace Core {

namespace Internal {
class IDocumentPrivate;
}

class CORE_EXPORT IDocument : public QObject
{
    Q_OBJECT

public:
    enum ReloadSetting {
        AlwaysAsk = 0,
        ReloadUnmodified = 1,
        IgnoreAll = 2,
        ReloadUnmodifiedImmediately = 3
    };

    enum ChangeTrigger {
        TriggerInternal,
        TriggerExternal
    };

    enum ChangeType {
        TypeContents,
        TypeRemoved
    };

    enum ReloadBehavior {
        BehaviorAsk,
        BehaviorSilent
    };

    enum ReloadFlag {
        FlagReload,
        FlagIgnore
    };

    enum class SaveOption { AutoSave, DisableFormatOnSave, None };

    IDocument(QObject *parent = nullptr);
    ~IDocument() override;

    void setId(Utils::Id id);
    Utils::Id id() const;

    virtual Utils::Result<> open(const Utils::FilePath &filePath, const Utils::FilePath &realFilePath);

    Utils::Result<> save(const Utils::FilePath &filePath = {}, SaveOption option = SaveOption::None);

    virtual QByteArray contents() const;
    virtual Utils::Result<> setContents(const QByteArray &contents);
    virtual void formatContents();

    const Utils::FilePath &filePath() const;
    virtual void setFilePath(const Utils::FilePath &filePath);
    QString displayName() const;
    void setPreferredDisplayName(const QString &name);
    QString preferredDisplayName() const;
    QString plainDisplayName() const;
    void setUniqueDisplayName(const QString &name);
    QString uniqueDisplayName() const;

    QString toolTip() const;

    bool isFileReadOnly() const;
    bool isTemporary() const;
    void setTemporary(bool temporary);

    virtual Utils::FilePath fallbackSaveAsPath() const;
    virtual QString fallbackSaveAsFileName() const;

    QString mimeType() const;
    void setMimeType(const QString &mimeType);

    virtual bool shouldAutoSave() const;
    virtual bool isModified() const;
    virtual bool isSaveAsAllowed() const;
    // Whether this kind of file belongs in Design mode. It is a property of
    // the file, not of the view showing it, so that a view which is not a
    // widget answers the same.
    virtual bool isDesignModePreferred() const;
    virtual bool isSaveAsNeeded() const;
    bool isSuspendAllowed() const;
    void setSuspendAllowed(bool value);

    virtual ReloadBehavior reloadBehavior(ChangeTrigger state, ChangeType type) const;
    virtual Utils::Result<> reload(ReloadFlag flag, ChangeType type);

    void checkPermissions();

    Utils::Result<> autoSave(const Utils::FilePath &filePath);
    void setRestoredFrom(const Utils::FilePath &path);
    void removeAutoSaveFile();

    bool hasWriteWarning() const;
    void setWriteWarning(bool has);

    bool isConflicted() const;
    void setConflicted(bool conflicted);

    Utils::InfoBar *infoBar();
    Utils::MinimizableInfoBars *minimizableInfoBars();
    // The ones this document already has, or nullptr. Unlike the
    // non-const overload this does not create them, so asking is free
    // for the documents - nearly all of them - that never use any.
    const Utils::MinimizableInfoBars *minimizableInfoBars() const;

signals:
    // For meta data changes: file name, modified state, ...
    void changed();

    // For changes in the contents of the document
    void contentsChanged();

    void mimeTypeChanged();

    void aboutToReload();
    void reloadFinished(bool success);
    void conflictedChanged(bool conflicted);
    void aboutToSave(const Utils::FilePath &filePath, SaveOption option);
    void saved(const Utils::FilePath &filePath, SaveOption option);

    void filePathChanged(const Utils::FilePath &oldName, const Utils::FilePath &newName);

protected:
    virtual Utils::Result<> saveImpl(
        const Utils::FilePath &filePath = {}, SaveOption option = SaveOption::None);

private:
    Internal::IDocumentPrivate *d;
};

} // namespace Core

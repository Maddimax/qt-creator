// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "aspects.h"
#include "covariantcallback.h"

namespace Utils {

namespace Internal { class AspectListPrivate; }

class QTCREATOR_UTILS_EXPORT AspectList : public Utils::BaseAspect
{
    Q_OBJECT
    friend class Internal::AspectListPrivate;

    // Which item the details pane is about and the buttons act on. The view
    // says so and the aspect answers what may be done to it, so that a
    // QTreeView and a Qt Quick ListView agree. -1 when nothing is picked.
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(bool canMoveUp READ canMoveUp NOTIFY currentIndexChanged)
    Q_PROPERTY(bool canMoveDown READ canMoveDown NOTIFY currentIndexChanged)

public:
    using CreateItem = std::function<std::shared_ptr<BaseAspect>()>;

    AspectList(Utils::AspectContainer *container = nullptr);
    ~AspectList() override;

    AspectPresentation presentation() const override;

    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;

    void volatileToMap(Utils::Store &map) const override;
    QVariantList toList(bool v) const;

    QList<std::shared_ptr<BaseAspect>> items() const;
    QList<std::shared_ptr<BaseAspect>> volatileItems() const;

    std::shared_ptr<BaseAspect> createAndAddItem();
    std::shared_ptr<BaseAspect> addItem(const std::shared_ptr<BaseAspect> &item);
    std::shared_ptr<BaseAspect> actualAddItem(const std::shared_ptr<BaseAspect> &item);

    void removeItem(const std::shared_ptr<BaseAspect> &item);
    void actualRemoveItem(const std::shared_ptr<BaseAspect> &item);
    void clear();

    void apply() override;
    void cancel() override;
    void setAutoApply(bool on) override;

    void setCreateItemFunction(CreateItem createItem);

    void forEachItem(const CovariantCallback<void(std::shared_ptr<BaseAspect>)> &callback) const
    {
        for (const auto &item : volatileItems())
            callback(item);
    }

    void forEachItem(const CovariantCallback<void(std::shared_ptr<BaseAspect>, int)> &callback) const
    {
        int idx = 0;
        for (const auto &item : volatileItems())
            callback(item, idx++);
    }

    qsizetype size() const;
    bool isDirty() const override;

    QVariant variantValue() const override { return toList(false); }
    void setVariantValue(const QVariant &value, Announcement howToAnnounce = DoEmit) override;
    QVariant volatileVariantValue() const override { return {}; } // ??

    enum class DisplayStyle { InlineList, ListViewWithDetails };
    void setDisplayStyle(DisplayStyle displayStyle);

    // Whether the items mean anything in the order they are in. Off unless a
    // page says so; on, the list offers Move Up and Move Down.
    void setOrdered(bool ordered);
    bool isOrdered() const;

    // The rows a view shows. Not owned: the aspect keeps it in step with its
    // items, including the ones that have been removed but not yet applied.
    QAbstractItemModel *itemModel() const;
    // A row of that model is not an index into volatileItems(): a removed item
    // keeps its row, struck through, until the page is applied. These two are
    // how a view crosses between the one and the other.
    std::shared_ptr<BaseAspect> itemForRow(int row) const;
    int rowForItem(const std::shared_ptr<BaseAspect> &item) const;

    int currentIndex() const;
    void setCurrentIndex(int index);
    bool canMoveUp() const;
    bool canMoveDown() const;
    Q_INVOKABLE void moveCurrentUp();
    Q_INVOKABLE void moveCurrentDown();
    void moveItem(int from, int to);

    void addExtraButton(const QString &text, std::function<void()> callback);
    QStringList extraButtonTexts() const;
    void triggerExtraButton(int index);

    CovariantCallback<QVariant(BaseAspect *, int)> listViewDataCallback;

    CovariantCallback<void(std::shared_ptr<BaseAspect>)> itemAddedCallback;
    CovariantCallback<void(std::shared_ptr<BaseAspect>)> itemRemovedCallback;

signals:
    void volatileItemListChanged();
    void currentIndexChanged(int index);

private:
    std::unique_ptr<Internal::AspectListPrivate> d;
};

} // namespace Utils

// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qbskitaspect.h"

#include "customqbspropertiesdialog.h"
#include "qbsprofilemanager.h"
#include "qbsprojectmanagertr.h"

#include <projectexplorer/kit.h>
#include <projectexplorer/kitaspect.h>

#include <utils/aspectwidgets.h>
#include <utils/elidinglabel.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>

#include <QPushButton>

using namespace ProjectExplorer;

namespace QbsProjectManager::Internal {

class QbsKitAspectImpl final : public KitAspect
{
public:
    QbsKitAspectImpl(Kit *kit, const KitAspectFactory *kitInfo)
        : KitAspect(kit, kitInfo)
    {
        // Properties are edited in a dialog, so the row is a summary of them
        // and the one button that opens it.
        m_properties = addControl<Utils::ActionAspect>();
        m_properties->setActionText(Tr::tr("Change..."));
        m_properties->setSummaryProvider([this] { return QbsKitAspect::representation(this->kit()); });
        m_properties->setAction([this] { changeProperties(); });
    }

private:
    void makeReadOnly(bool readOnly) override { m_properties->setEnabled(!readOnly); }
    void refresh() override { m_properties->updateSummary(); }


    void changeProperties()
    {
        CustomQbsPropertiesDialog dlg(QbsKitAspect::properties(kit()));
        if (dlg.exec() == QDialog::Accepted) {
            QbsKitAspect::setProperties(kit(), dlg.properties());
            refresh();
        }
    }

    Utils::ActionAspect *m_properties = nullptr;
};

QString QbsKitAspect::representation(const Kit *kit)
{
    const QVariantMap props = properties(kit);
    QString repr;
    for (auto it = props.begin(); it != props.end(); ++it) {
        if (!repr.isEmpty())
            repr += ' ';
        repr += it.key() + ':' + toJSLiteral(it.value());
    }
    return repr;
}

QVariantMap QbsKitAspect::properties(const Kit *kit)
{
    QTC_ASSERT(kit, return QVariantMap());
    return kit->value(id()).toMap();
}

void QbsKitAspect::setProperties(Kit *kit, const QVariantMap &properties)
{
    QTC_ASSERT(kit, return);
    kit->setValue(id(), properties);
}

Utils::Id QbsKitAspect::id()
{
    return "Qbs.KitInformation";
}

// QbsKitAspectFactory

class QbsKitAspectFactory final : public KitAspectFactory
{
public:
    QbsKitAspectFactory()
    {
        setId(QbsKitAspect::id());
        setDisplayName(Tr::tr("Qbs Profile Additions"));
        setDescription(Tr::tr("Additional module properties to set in "
                              "the Qbs profile corresponding to this kit.\n"
                              "You will rarely need to do this."));
        setPriority(22000);
    }

private:
    Tasks validate(const Kit *) const override { return {}; }

    ItemList toUserOutput(const Kit *k) const override
    {
        return {{displayName(), QbsKitAspect::representation(k)}};
    }

    KitAspect *createKitAspect(Kit *k) const override
    {
        return new QbsKitAspectImpl(k, this);
    }
};

const QbsKitAspectFactory theQbsKitAspectFactory;

} // QbsProjectManager::Internal

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"

#include "abi.h"

#include <utils/aspects.h>

namespace ProjectExplorer {

// An ABI, as something to pick rather than as widgets to pick it with: one
// choice of the ABIs a toolchain reported, plus the five parts of a custom one
// for when none of them is right. Seven places ask for an ABI; the state is
// here so that each of them can draw it however it likes.
class PROJECTEXPLORER_EXPORT AbiAspects : public Utils::AspectContainer
{
    Q_OBJECT

public:
    explicit AbiAspects(Utils::AspectContainer *container = nullptr);

    // The ABIs to offer and which of them to start on. An empty list leaves
    // only the custom one, which is what a toolchain that reported nothing
    // gets.
    void setAbis(const Abis &abis, const Abi &currentAbi);

    Abis supportedAbis() const;
    // Whether the parts below are what counts, rather than one of the offered
    // ABIs.
    bool isCustomAbi() const;
    Abi currentAbi() const;

    // The offered ABIs, hidden where there are none to offer.
    Utils::SelectionAspect &choice() { return m_abi; }
    // The five parts of a custom ABI, in the order they are written in.
    QList<Utils::SelectionAspect *> parts();

signals:
    void abiChanged();

private:
    void updateFromChoice();
    void updateFromParts();
    void setPartsTo(const Abi &abi);
    void refreshFlavors(Abi::OS os);
    Abi abiFromParts() const;

    // Filling the boxes in is not the user picking, and the parts are written
    // from the choice and the choice from the parts.
    bool m_updating = false;
    Abi m_currentAbi{Abi::UnknownArchitecture};

    Utils::SelectionAspect m_abi{this};
    Utils::SelectionAspect m_architecture{this};
    Utils::SelectionAspect m_os{this};
    Utils::SelectionAspect m_osFlavor{this};
    Utils::SelectionAspect m_binaryFormat{this};
    Utils::SelectionAspect m_wordWidth{this};
};

#ifdef WITH_TESTS
QObject *createAbiAspectsTest();
#endif

} // namespace ProjectExplorer

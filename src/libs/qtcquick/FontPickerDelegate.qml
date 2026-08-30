// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A font, chosen as one thing: a family and a size that know about each other.
// Drawn from the two aspects a FontAspect holds, because the container itself
// has no value a generic delegate could bind to.
//
// The sizes on offer are the ones the *current* family has. A family with
// fixed sizes has only those, so changing the family can leave the chosen size
// unavailable - and then the nearest one it does have is shown instead.
RowLayout {
    id: root

    required property Aspect aspect

    readonly property var familyAspect: root.aspect
                                        ? AspectModels.fontFamilyAspect(root.aspect) : null
    readonly property var sizeAspect: root.aspect
                                      ? AspectModels.fontPointSizeAspect(root.aspect) : null

    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool aspectEnabled: (aspect?.enabled ?? false)
                                          && !(aspect?.readOnly ?? true)
    readonly property string toolTip: aspect?.toolTip ?? ""

    readonly property var families: root.familyAspect
                                    ? AspectModels.fontFamilies(root.familyAspect) : []

    // Both of these call into C++, and a call creates no binding of its own -
    // so what they depend on is named here, or they are computed once and
    // never again. Which sizes exist follows the family; which of them is
    // shown follows the family *and* the size.
    readonly property var pointSizes: {
        void root.familyAspect?.value
        return root.aspect ? AspectModels.fontPointSizes(root.aspect) : []
    }

    readonly property int shownSize: {
        void root.familyAspect?.value
        void root.sizeAspect?.value
        return root.aspect ? AspectModels.closestFontPointSize(root.aspect) : -1
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    FormLabel {
        text: root.familyAspect?.plainLabelText ?? ""
    }

    ComboBox {
        id: familyBox

        objectName: "fontFamily"

        enabled: root.aspectEnabled
        model: root.families
        currentIndex: root.families.indexOf(root.familyAspect?.value ?? "")
        ToolTip.text: root.toolTip
        ToolTip.visible: hovered && root.toolTip !== ""
        Layout.preferredWidth: Metrics.formControlWidth

        onActivated: (index) => {
            if (root.familyAspect)
                root.familyAspect.value = familyBox.textAt(index)
        }
    }

    QtcLabel {
        text: root.sizeAspect?.plainLabelText ?? ""
    }

    ComboBox {
        id: sizeBox

        objectName: "fontPointSize"

        enabled: root.aspectEnabled && root.pointSizes.length > 0
        model: root.pointSizes
        // Not the size the aspect holds: the family may not have it.
        currentIndex: root.pointSizes.indexOf(root.shownSize)
        Layout.preferredWidth: Metrics.formControlWidth / 2

        onActivated: (index) => {
            if (root.sizeAspect)
                root.sizeAspect.value = root.pointSizes[index]
        }
    }

    Item { Layout.fillWidth: true }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("HarmonyOS SDK")

        ColumnLayout {
            TextDisplayDelegate { aspect: root.aspects.Instruction }

            RowLayout {
                StringDelegate { aspect: root.aspects.SdkLocation }
                ButtonDelegate { aspect: root.aspects.Autodetect }
            }

            TextDisplayDelegate { aspect: root.aspects.Status }
            StringDelegate { aspect: root.aspects.AdditionalPackages }
            BoolDelegate { aspect: root.aspects.AutomaticKitCreation }
        }
    }

    AspectGroupBox {
        title: qsTr("Running")

        ColumnLayout {
            BoolDelegate { aspect: aspects.RunWithoutInstalling }
        }
    }

    AspectGroupBox {
        title: qsTr("Package Signing")

        ColumnLayout {
            TextDisplayDelegate { aspect: root.aspects.SigningNote }
            StringDelegate { aspect: root.aspects.SigningCertificate }
            StringDelegate { aspect: root.aspects.SigningProfile }
            StringDelegate { aspect: root.aspects.SigningKeystore }
            StringDelegate { aspect: root.aspects.SigningKeyAlias }
            SecretDelegate { aspect: root.aspects.SigningKeyPassword }
            SecretDelegate { aspect: root.aspects.SigningStorePassword }
        }
    }
}

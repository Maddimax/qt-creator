// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    AspectGroupBox {
        title: qsTr("HarmonyOS SDK")

        ColumnLayout {
            TextDisplayDelegate { aspect: aspects.Instruction }

            RowLayout {
                StringDelegate { aspect: aspects.SdkLocation }
                ButtonDelegate { aspect: aspects.Autodetect }
            }

            TextDisplayDelegate { aspect: aspects.Status }
            StringDelegate { aspect: aspects.AdditionalPackages }
            BoolDelegate { aspect: aspects.AutomaticKitCreation }
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
            TextDisplayDelegate { aspect: aspects.SigningNote }
            StringDelegate { aspect: aspects.SigningCertificate }
            StringDelegate { aspect: aspects.SigningProfile }
            StringDelegate { aspect: aspects.SigningKeystore }
            StringDelegate { aspect: aspects.SigningKeyAlias }
            SecretDelegate { aspect: aspects.SigningKeyPassword }
            SecretDelegate { aspect: aspects.SigningStorePassword }
        }
    }
}

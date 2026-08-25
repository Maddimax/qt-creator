// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    readonly property var android: AspectModels.named(aspects.Android)
    readonly property var openSsl: AspectModels.named(aspects.OpenSsl)

    TextDisplayDelegate { aspect: root.aspects.Info }

    AspectGroupBox {
        title: root.aspects.Android?.plainLabelText ?? ""

        // Each path has somewhere to get it from beside it.
        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true

            StringDelegate {
                aspect: root.android.JdkLocation
                Layout.fillWidth: true
            }

            ButtonDelegate {
                aspect: root.android.DownloadJdk
                Layout.fillWidth: false
            }
        }

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true

            StringDelegate {
                aspect: root.android.SdkLocation
                Layout.fillWidth: true
            }

            ButtonDelegate {
                aspect: root.android.SetUpSdk
                Layout.fillWidth: false
            }

            ButtonDelegate {
                aspect: root.android.DownloadSdk
                Layout.fillWidth: false
            }
        }

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            ButtonDelegate {
                aspect: root.android.SdkManager
                Layout.fillWidth: false
            }
        }

        // Which NDKs there are, where each came from, and which one every kit
        // is forced onto.
        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true

            TreeDelegate {
                id: ndks

                aspect: root.android.NdkList
                Layout.fillWidth: true
                Layout.minimumHeight: Metrics.chooserListHeight

                onCurrentIndexChanged: root.android.NdkList.setCurrentIndex(currentIndex)
            }

            ColumnLayout {
                spacing: Spacing.GapVXs
                Layout.alignment: Qt.AlignTop

                ButtonDelegate {
                    aspect: root.android.AddNdk
                    Layout.fillWidth: false
                }

                ButtonDelegate {
                    aspect: root.android.RemoveNdk
                    Layout.fillWidth: false
                }

                ButtonDelegate {
                    aspect: root.android.MakeDefaultNdk
                    Layout.fillWidth: false
                }

                ButtonDelegate {
                    aspect: root.android.DownloadNdk
                    Layout.fillWidth: false
                }
            }
        }

        // Everything that has to be true before Android can be built for, and
        // which of it is not.
        GroupDelegate { aspect: root.android.AndroidSummary }

        BoolDelegate { aspect: root.android.CreateKit }
    }

    AspectGroupBox {
        title: root.aspects.OpenSsl?.plainLabelText ?? ""

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true

            StringDelegate {
                aspect: root.openSsl.OpenSslLocation
                Layout.fillWidth: true
            }

            ButtonDelegate {
                aspect: root.openSsl.DownloadOpenSsl
                Layout.fillWidth: false
            }
        }

        GroupDelegate { aspect: root.openSsl.OpenSslSummary }
    }
}

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// The C++ code style when ClangFormat is doing the formatting: the
// .clang-format file itself on the left, and what it does to code on the
// right. Editing the file re-indents the preview, which is the only way to see
// what an option means.
AspectPage {
    id: root

    contentFillsHeight: true

    // The plugin's own settings block, shown above the style: see
    // Utils::ContainerAspect.
    readonly property var global: AspectModels.named(root.aspects.Global.container)

    CodeStyleSelector { aspects: root.aspects }

    AspectGroupBox {
        title: qsTr("ClangFormat")

        BoolDelegate { aspect: root.global.UseGlobalSettings }
        BoolDelegate { aspect: root.global.UseClangFormat }
        IntegerDelegate { aspect: root.global.FileSizeThreshold }
        SelectionDelegate { aspect: root.global.FormattingMode }

        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.leftMargin: Spacing.PaddingHL

            BoolDelegate { aspect: root.global.FormatWhileTyping }
            BoolDelegate { aspect: root.global.FormatOnSave }
        }

        BoolDelegate { aspect: root.global.UseCustomSettings }

        TextDisplayDelegate { aspect: root.global.ProjectHasClangFormat }
        TextDisplayDelegate { aspect: root.global.ProjectFileNote }
    }

    TextDisplayDelegate { aspect: root.aspects.ClangVersion }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.fillWidth: true
            Layout.fillHeight: true

            // The .clang-format file's text, held apart from any view of it.
            // Told what it is rather than guessing from the name, which for a
            // file called .clang-format is the only way to know.
            CodeBuffer {
                id: styleFile

                objectName: "clangFormatStyleFile"
                text: root.aspects.StyleText.value ?? ""
                mimeType: "text/x-yaml"
            }

            CodeViewport {
                id: styleEditor

                objectName: "clangFormatStyleEditor"
                source: styleFile
                readOnly: !(root.aspects.Editable.value ?? false)
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: Metrics.formTextAreaHeight

                // Written back when focus leaves rather than on every
                // keystroke: re-indenting the preview rewrites a document, and
                // that must not happen under the cursor.
                onEditingFinished: {
                    if (styleFile.text !== root.aspects.StyleText.value)
                        root.aspects.StyleText.value = styleFile.text
                }
            }

            TextDisplayDelegate { aspect: root.aspects.FileProblem }
        }

        CodeStylePreview { aspects: root.aspects }
    }
}

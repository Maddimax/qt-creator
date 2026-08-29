import qbs.FileInfo
import qbs.Environment

Project {
    references: "texteditorsupport.qbs"
    QtcPlugin {
        name: "TextEditor"

        Depends { name: "Qt"; submodules: ["widgets", "xml", "network", "printsupport"] }
        Depends { name: "TextEditorSupport" }
        Depends { name: "Utils" }
        Depends { name: "QtcQuick" }
        Depends { name: "KSyntaxHighlighting" }

        Export {
            Depends { name: "KSyntaxHighlighting" }
            Depends { name: "TextEditorSupport" }
        }

        Depends { name: "Core" }

        cpp.enableExceptions: true

        files: [
            "autocompleter.cpp",
            "autocompleter.h",
            "basefilefind.cpp",
            "basefilefind.h",
            "basehoverhandler.cpp",
            "basehoverhandler.h",
            "behaviorsettings.cpp",
            "behaviorsettings.h",
            "blockrange.h",
            "blockselection.cpp",
            "blockselection.h",
            "bookmark.cpp",
            "bookmark.h",
            "bookmarkfilter.cpp",
            "bookmarkfilter.h",
            "bookmarkmanager.cpp",
            "bookmarkmanager.h",
            "circularclipboard.cpp",
            "circularclipboard.h",
            "circularclipboardassist.cpp",
            "circularclipboardassist.h",
            "codecchooser.cpp",
            "codecchooser.h",
            "codehighlighting.cpp",
            "codehighlighting.h",
            "codebuffer.cpp",
            "codebuffer.h",
            "codecompletion.cpp",
            "codecompletion.h",
            "codedocument.cpp",
            "codedocument.h",
            "codeindenting.cpp",
            "codeindenting.h",
            "codesource.cpp",
            "codesource.h",
            "codestyleeditor.cpp",
            "codestyleeditor.h",
            "codestylepool.cpp",
            "codestylepool.h",
            "colorpreviewhoverhandler.cpp",
            "colorpreviewhoverhandler.h",
            "colorscheme.cpp",
            "colorscheme.h",
            "colorschemeedit.cpp",
            "colorschemeedit.h",
            "command.cpp",
            "command.h",
            "commentssettings.cpp",
            "commentssettings.h",
            "completionsettings.cpp",
            "completionsettings.h",
            "displaysettings.cpp",
            "displaysettings.h",
            "extraencodingsettings.cpp",
            "extraencodingsettings.h",
            "findincurrentfile.cpp",
            "findincurrentfile.h",
            "findinfiles.cpp",
            "findinfiles.h",
            "findinopenfiles.cpp",
            "findinopenfiles.h",
            "fontsettings.cpp",
            "fontsettings.h",
            "fontsettingspage.cpp",
            "fontsettingspage.h",
            "formatter.h",
            "formattexteditor.cpp",
            "formattexteditor.h",
            "gutterframe.cpp",
            "gutterframe.h",
            "highlighter.cpp",
            "highlighter.h",
            "hoverhandlerrunner.cpp",
            "hoverhandlerrunner.h",
            "highlighterhelper.cpp",
            "highlighterhelper.h",
            "highlightersettings.cpp",
            "highlightersettings.h",
            "icodestylepreferences.cpp",
            "icodestylepreferences.h",
            "icodestylepreferencesfactory.cpp",
            "icodestylepreferencesfactory.h",
            "indenter.h",
            "inlinediffdecorator.cpp",
            "inlinediffdecorator.h",
            "ioutlinewidget.h",
            "jsoneditor.cpp",
            "jsoneditor.h",
            "linenumberfilter.cpp",
            "linenumberfilter.h",
            "marginsettings.cpp",
            "marginsettings.h",
            "markdowneditor.cpp",
            "markdowneditor.h",
            "mergeconflict.cpp",
            "mergeconflict.h",
            "outlinefactory.cpp",
            "outlinefactory.h",
            "plaintexteditorfactory.cpp",
            "plaintexteditorfactory.h",
            "quickfix.cpp",
            "quickfix.h",
            "quicktexteditor.cpp",
            "quicktexteditor.h",
            "refactoringchanges.cpp",
            "refactoringchanges.h",
            "refactoroverlay.cpp",
            "refactoroverlay.h",
            "semantichighlighter.cpp",
            "semantichighlighter.h",
            "storagesettings.cpp",
            "storagesettings.h",
            "symbolrequests.h",
            "syntaxhighlighter.cpp",
            "syntaxhighlighter.h",
            "tabsettings.cpp",
            "tabsettings.h",
            "textdocument.cpp",
            "textdocument.h",
            "textdocumentlayout.cpp",
            "textdocumentlayout.h",
            "texteditor.cpp",
            "texteditor.h",
            "texteditor_global.h",
            "texteditorconstants.cpp",
            "texteditorconstants.h",
            "texteditoroverlay.cpp",
            "texteditoroverlay.h",
            "texteditorplugin.cpp",
            "texteditortr.h",
            "minimapview.cpp",
            "minimapview.h",
            "textviewport.cpp",
            "textviewport.h",
            "textindenter.cpp",
            "textindenter.h",
            "textmark.cpp",
            "textmark.h",
            "textoperations.cpp",
            "textoperations.h",
            "textstyles.h",
            "textsuggestion.cpp",
            "textsuggestion.h",
            "typehierarchy.cpp",
            "typehierarchy.h",
            "typingsettings.cpp",
            "typingsettings.h",
        ]
    // qbs has no QML module support; the .qml files are built by CMake only.
    Group {
        name: "qml"
        files: ["*.qml"]
        fileTags: []
    }


        Group {
            name: "CodeAssist"
            prefix: "codeassist/"
            files: [
                "assistenums.h",
                "assistinterface.cpp",
                "assistinterface.h",
                "assistproposalitem.cpp",
                "assistproposalitem.h",
                "assistproposaliteminterface.h",
                "assisttarget.cpp",
                "assisttarget.h",
                "asyncprocessor.cpp",
                "asyncprocessor.h",
                "codeassistant.cpp",
                "codeassistant.h",
                "completionassistprovider.cpp",
                "completionassistprovider.h",
                "documentcontentcompletion.cpp",
                "documentcontentcompletion.h",
                "functionhintproposal.cpp",
                "functionhintproposal.h",
                "functionhintproposalwidget.cpp",
                "functionhintproposalwidget.h",
                "genericproposal.cpp",
                "genericproposal.h",
                "genericproposalmodel.cpp",
                "genericproposalmodel.h",
                "genericproposalwidget.cpp",
                "genericproposalwidget.h",
                "iassistprocessor.cpp",
                "iassistprocessor.h",
                "iassistproposal.cpp",
                "iassistproposal.h",
                "iassistproposalmodel.cpp",
                "iassistproposalmodel.h",
                "iassistproposalwidget.cpp",
                "iassistproposalwidget.h",
                "iassistprovider.cpp",
                "iassistprovider.h",
                "ifunctionhintproposalmodel.cpp",
                "ifunctionhintproposalmodel.h",
                "keywordscompletionassist.cpp",
                "keywordscompletionassist.h",
            ]
        }

        Group {
            name: "Snippets"
            prefix: "snippets/"
            files: [
                "reuse.h",
                "snippet.cpp",
                "snippet.h",
                "snippetassistcollector.cpp",
                "snippetassistcollector.h",
                "snippeteditor.cpp",
                "snippeteditor.h",
                "snippetoverlay.cpp",
                "snippetoverlay.h",
                "snippetparser.cpp",
                "snippetparser.h",
                "snippetprovider.cpp",
                "snippetprovider.h",
                "snippetscollection.cpp",
                "snippetscollection.h",
                "snippetssettingspage.cpp",
                "snippetssettingspage.h",
            ]
        }

        QtcTestFiles {
            files: [
                "codeassist/codeassist_test.cpp",
                "codeassist/codeassist_test.h",
                "codehighlighting_test.cpp",
                "codehighlighting_test.h",
                "codestyleaspect_test.cpp",
                "codestyleaspect_test.h",
                "highlighter_test.cpp",
                "highlighter_test.h",
                "mergeconflict_test.cpp",
                "mergeconflict_test.h",
                "spellcheck_test.cpp",
                "spellcheck_test.h",
                "texteditor_test.cpp",
                "texteditor_test.h",
                "textviewport_test.cpp",
                "textviewport_test.h",
            ]
        }

        Group {
            name: "images"
            fileTags: "qt.core.resource_data"
            files: [
                "images/finddocuments.png",
                "images/snippet.png",
                "images/settingscategory_texteditor.png",
                "images/settingscategory_texteditor@2x.png",
            ]
        }
    }
}

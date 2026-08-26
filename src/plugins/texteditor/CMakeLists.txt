if(TARGET KSyntaxHighlighting)
  set(KSYNTAXHIGHLIGHTING_TARGET KSyntaxHighlighting)
else()
  set(KSYNTAXHIGHLIGHTING_TARGET KF5::SyntaxHighlighting)
endif()

add_qtc_library(TextEditorSupport STATIC
  DEPENDS Utils
  SOURCES
    texteditorsupport_global.h
    tabsettingsdata.cpp
)

add_qtc_plugin(TextEditor
  DEPENDS Qt::Concurrent Qt::Network Qt::PrintSupport Qt::Xml
  PUBLIC_DEPENDS ${KSYNTAXHIGHLIGHTING_TARGET}
  PLUGIN_DEPENDS Core
  SOURCES
    autocompleter.cpp autocompleter.h
    basefilefind.cpp basefilefind.h
    basehoverhandler.cpp basehoverhandler.h
    behaviorsettings.cpp behaviorsettings.h
    blockrange.h
    bookmark.cpp bookmark.h
    bookmarkfilter.cpp bookmarkfilter.h
    bookmarkmanager.cpp bookmarkmanager.h
    circularclipboard.cpp circularclipboard.h
    circularclipboardassist.cpp circularclipboardassist.h
    codeassist/assistenums.h
    codeassist/assistinterface.cpp codeassist/assistinterface.h
    codeassist/assistproposalitem.cpp codeassist/assistproposalitem.h
    codeassist/assistproposaliteminterface.h
    codeassist/assisttarget.cpp codeassist/assisttarget.h
    codeassist/asyncprocessor.cpp codeassist/asyncprocessor.h
    codeassist/codeassistant.cpp codeassist/codeassistant.h
    codeassist/completionassistprovider.cpp codeassist/completionassistprovider.h
    codeassist/documentcontentcompletion.cpp codeassist/documentcontentcompletion.h
    codeassist/functionhintproposal.cpp codeassist/functionhintproposal.h
    codeassist/functionhintproposalwidget.cpp codeassist/functionhintproposalwidget.h
    codeassist/genericproposal.cpp codeassist/genericproposal.h
    codeassist/genericproposalmodel.cpp codeassist/genericproposalmodel.h
    codeassist/genericproposalwidget.cpp codeassist/genericproposalwidget.h
    codeassist/iassistprocessor.cpp codeassist/iassistprocessor.h
    codeassist/iassistproposal.cpp codeassist/iassistproposal.h
    codeassist/iassistproposalmodel.cpp codeassist/iassistproposalmodel.h
    codeassist/iassistproposalwidget.cpp codeassist/iassistproposalwidget.h
    codeassist/iassistprovider.cpp codeassist/iassistprovider.h
    codeassist/ifunctionhintproposalmodel.cpp codeassist/ifunctionhintproposalmodel.h
    codeassist/keywordscompletionassist.cpp codeassist/keywordscompletionassist.h
    codecchooser.cpp codecchooser.h
    codestyleeditor.cpp codestyleeditor.h
    codestylepool.cpp codestylepool.h
    codestyleselectorwidget.cpp codestyleselectorwidget.h
    colorpreviewhoverhandler.cpp colorpreviewhoverhandler.h
    colorscheme.cpp colorscheme.h
    colorschemeedit.cpp colorschemeedit.h
    command.cpp command.h
    commentssettings.cpp commentssettings.h
    completionsettings.cpp completionsettings.h
    displaysettings.cpp displaysettings.h
    extraencodingsettings.cpp extraencodingsettings.h
    findincurrentfile.cpp findincurrentfile.h
    findinfiles.cpp findinfiles.h
    findinopenfiles.cpp findinopenfiles.h
    fontsettings.cpp fontsettings.h
    fontsettingspage.cpp fontsettingspage.h
    formatter.h
    formattexteditor.cpp formattexteditor.h
    gutterframe.cpp gutterframe.h
    highlighter.cpp highlighter.h
    highlighterhelper.cpp highlighterhelper.h
    highlightersettings.cpp highlightersettings.h
    icodestylepreferences.cpp icodestylepreferences.h
    icodestylepreferencesfactory.cpp icodestylepreferencesfactory.h
    indenter.h
    inlinediffdecorator.cpp inlinediffdecorator.h
    ioutlinewidget.h
    jsoneditor.cpp jsoneditor.h
    linenumberfilter.cpp linenumberfilter.h
    marginsettings.cpp marginsettings.h
    markdowneditor.cpp markdowneditor.h
    mergeconflict.cpp mergeconflict.h
    outlinefactory.cpp outlinefactory.h
    plaintexteditorfactory.cpp plaintexteditorfactory.h
    quickfix.cpp quickfix.h
    refactoringchanges.cpp refactoringchanges.h
    refactoroverlay.cpp refactoroverlay.h
    semantichighlighter.cpp semantichighlighter.h
    snippets/reuse.h
    snippets/snippet.cpp snippets/snippet.h
    snippets/snippetassistcollector.cpp snippets/snippetassistcollector.h
    snippets/snippeteditor.cpp snippets/snippeteditor.h
    snippets/snippetoverlay.cpp snippets/snippetoverlay.h
    snippets/snippetparser.cpp snippets/snippetparser.h
    snippets/snippetprovider.cpp snippets/snippetprovider.h
    snippets/snippetscollection.cpp snippets/snippetscollection.h
    snippets/snippetssettingspage.cpp snippets/snippetssettingspage.h
    storagesettings.cpp storagesettings.h
    syntaxhighlighter.cpp syntaxhighlighter.h
    tabsettings.cpp tabsettings.h
    textdocument.cpp textdocument.h
    textdocumentlayout.cpp textdocumentlayout.h
    texteditor.cpp texteditor.h
    texteditor_global.h
    texteditorconstants.cpp texteditorconstants.h
    texteditoroverlay.cpp texteditoroverlay.h
    texteditorplugin.cpp
    texteditortr.h
    textindenter.cpp textindenter.h
    textmark.cpp textmark.h
    textstyles.h
    textsuggestion.h textsuggestion.cpp
    typehierarchy.cpp typehierarchy.h
    typingsettings.cpp typingsettings.h
)

extend_qtc_plugin(TextEditor
  CONDITION WITH_TESTS
  SOURCES
    codeassist/codeassist_test.cpp
    codeassist/codeassist_test.h
    codestyleaspect_test.cpp
    codestyleaspect_test.h
    highlighter_test.cpp
    highlighter_test.h
    mergeconflict_test.cpp
    mergeconflict_test.h
    spellcheck_test.cpp
    spellcheck_test.h
    texteditor_test.cpp
    texteditor_test.h
)

qtc_add_resources(TextEditor "images"
  PREFIX "/texteditor"
  BASE "."
  FILES
    images/finddocuments.png
    images/settingscategory_texteditor.png
    images/settingscategory_texteditor@2x.png
    images/snippet.png
)

# from cmake 3.24 $<LINK_LIBRARY:WHOLE_ARCHIVE,TextEditorSupport> is supported which can be added to DEPENDS
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0" AND NOT CMAKE_GENERATOR STREQUAL "FASTBuild")
  target_link_libraries(TextEditor PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,TextEditorSupport>)
else ()
  if (MSVC)
    qtc_output_binary_dir(_output_binary_dir)
    set_target_properties(TextEditor PROPERTIES LINK_FLAGS "/WHOLEARCHIVE:${_output_binary_dir}/${IDE_BIN_PATH}/TextEditorSupport.lib")
  elseif (APPLE)
    target_link_libraries(TextEditor PRIVATE -Wl,-force_load "$<TARGET_LINKER_FILE:TextEditorSupport>")
  else ()
    target_link_libraries(TextEditor PRIVATE -Wl,--whole-archive $<TARGET_LINKER_FILE:TextEditorSupport> -Wl,--no-whole-archive)
  endif ()
endif ()

# The Qt Quick side of the editor: highlighting for a TextEdit, and the
# viewport that draws a document with the scene graph. Needs Qt::Quick,
# which the plugin does not otherwise depend on.
extend_qtc_plugin(TextEditor
  CONDITION TARGET Qt::Quick
  DEPENDS Qt::Quick
  SOURCES
    codehighlighting.cpp codehighlighting.h
    codebuffer.cpp codebuffer.h
    codecompletion.cpp codecompletion.h
    codedocument.cpp codedocument.h
    codeindenting.cpp codeindenting.h
    codesource.cpp codesource.h
    textviewport.cpp textviewport.h
)

# The tests for the Qt Quick side. texteditorplugin.cpp registers them and is
# built whether or not Qt::Quick is there, so it needs to be told: without this
# a WITH_TESTS build of a Qt Creator without Qt Quick does not compile, because
# the headers it includes are not in the build at all.
extend_qtc_plugin(TextEditor
  CONDITION WITH_TESTS AND TARGET Qt::Quick
  DEFINES WITH_QUICK_TESTS
  SOURCES
    codehighlighting_test.cpp codehighlighting_test.h
    textviewport_test.cpp textviewport_test.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET TextEditor AND TARGET Qt::Quick)
  qt_add_qml_module(TextEditor
    URI QtCreator.TextEditor
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      BehaviorSettingsForm.qml
      BehaviorSettingsPage.qml
      CodeEditor.qml
      CodeStylePreview.qml
      CodeStyleProjectForm.qml
      CodeStyleSelector.qml
      CodeView.qml
      CodeViewport.qml
      CommentsSettingsPage.qml
      CompletionSettingsPage.qml
      DisplaySettingsPage.qml
      EncodingSettingsForm.qml
      FontSettingsPage.qml
      HighlighterSettingsPage.qml
      SnippetEditor.qml
      SnippetsSettingsPage.qml
      StorageSettingsForm.qml
      TabSettingsForm.qml
      TypingSettingsForm.qml
  )
endif()

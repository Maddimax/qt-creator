add_qtc_plugin(Nim
  PLUGIN_DEPENDS Core TextEditor ProjectExplorer
  SOURCES
    editor/nimcompletionassistprovider.cpp editor/nimcompletionassistprovider.h
    editor/nimeditorfactory.cpp editor/nimeditorfactory.h
    editor/nimhighlighter.cpp editor/nimhighlighter.h
    editor/nimindenter.cpp editor/nimindenter.h
    editor/nimtexteditorwidget.cpp editor/nimtexteditorwidget.h
    nim.qrc
    nimconstants.h
    nimplugin.cpp
    nimtr.h
    project/nimblebuildstep.h project/nimblebuildstep.cpp
    project/nimbleproject.h project/nimbleproject.cpp
    project/nimblerunconfiguration.h project/nimblerunconfiguration.cpp
    project/nimbletaskstep.h project/nimbletaskstep.cpp
    project/nimcompilerbuildstep.cpp project/nimcompilerbuildstep.h
    project/nimcompilercleanstep.cpp project/nimcompilercleanstep.h
    project/nimoutputtaskparser.cpp project/nimoutputtaskparser.h
    project/nimproject.cpp project/nimproject.h
    project/nimrunconfiguration.cpp project/nimrunconfiguration.h
    project/nimtoolchain.cpp project/nimtoolchain.h
    settings/nimcodestylesettingspage.cpp settings/nimcodestylesettingspage.h
    settings/nimsettings.cpp settings/nimsettings.h
    suggest/client.cpp suggest/client.h
    suggest/clientrequests.cpp suggest/clientrequests.h
    suggest/nimsuggest.cpp suggest/nimsuggest.h
    suggest/nimsuggestcache.cpp suggest/nimsuggestcache.h
    suggest/server.cpp suggest/server.h
    suggest/sexprlexer.h suggest/sexprparser.h
    tools/nimlexer.cpp tools/nimlexer.h
    tools/sourcecodestream.h
)

# The settings page's Qt Quick form. A module rather than a plain resource so
# that qmllint and qmlcachegen see the file; qbs cannot build QML modules, so
# it only lists it - the same split qtcquick already lives with.
if(TARGET Nim)
  qt_add_qml_module(Nim
    URI QtCreator.Nim
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      NimCodeStylePage.qml
      NimSettingsPage.qml
  )
endif()

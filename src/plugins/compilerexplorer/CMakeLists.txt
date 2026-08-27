add_qtc_plugin(CompilerExplorer
  PLUGIN_DEPENDS Core ProjectExplorer TextEditor
  DEPENDS Qt::Network TerminalLib
  SOURCES
    api/config.h
    api/compile.cpp
    api/compile.h
    api/compiler.cpp
    api/compiler.h
    api/compilerexplorerapi.h
    api/language.cpp
    api/language.h
    api/library.cpp
    api/library.h
    api/request.cpp
    api/request.h

    compilerexplorer.qrc

    compilerexploreraspects.cpp
    compilerexploreraspects.h
    compilerexplorerplugin.cpp
    compilerexplorertr.h
    compilerexplorereditor.cpp
    compilerexplorereditor.h
    compilerexplorersettings.cpp
    compilerexplorersettings.h
    compilerexploreroptions.cpp
    compilerexploreroptions.h

    logos/logos.qrc
)

# Settings forms' Qt Quick sources. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it only
# lists them - the same split qtcquick already lives with.
if(TARGET CompilerExplorer)
  qt_add_qml_module(CompilerExplorer
    URI QtCreator.CompilerExplorer
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      CompilerExplorerOptions.qml
  )
endif()

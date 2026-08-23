
add_qtc_plugin(Terminal
  PLUGIN_DEPENDS Core ProjectExplorer
  DEPENDS TerminalLib
  SOURCES
    consolehost.cpp consolehost.h
    shellintegration.cpp shellintegration.h
    shellmodel.cpp shellmodel.h
    shortcutmap.cpp shortcutmap.h
    terminal.qrc
    terminalconstants.h
    terminalicons.h
    terminalpane.cpp terminalpane.h
    terminalplugin.cpp
    terminalprocessimpl.cpp terminalprocessimpl.h
    terminalsettings.cpp terminalsettings.h
    terminaltr.h
    terminalwidget.cpp terminalwidget.h
  SBOM_ARGS
    USE_ATTRIBUTION_FILES
    CREATE_SBOM_FOR_EACH_ATTRIBUTION
    ATTRIBUTION_FILE_PATHS
      "${QtCreator_SOURCE_DIR}/qt_attributions.json"
    ATTRIBUTION_IDS
      terminal-shellintegrations
      terminal-shellintegrations-clink
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Terminal AND TARGET Qt::Quick)
  qt_add_qml_module(Terminal
    URI QtCreator.Terminal
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      TerminalSettingsPage.qml
  )
endif()

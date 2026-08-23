add_qtc_plugin(Copilot
  PLUGIN_DEPENDS Core LanguageClient ProjectExplorer
  LONG_DESCRIPTION_MD Description.md
  SOURCES
    copilot.qrc
    copilotclient.cpp copilotclient.h
    copilotconstants.h
    copilotplugin.cpp
    copilotsettings.cpp copilotsettings.h
    copilottr.h
    requests/checkstatus.h
    requests/getcompletions.h
    requests/signinconfirm.h
    requests/signininitiate.h
    requests/signout.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Copilot AND TARGET Qt::Quick)
  qt_add_qml_module(Copilot
    URI QtCreator.Copilot
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      CopilotSettingsPage.qml
  )
endif()

add_qtc_plugin(Zephyr
  PLUGIN_DEPENDS Core ProjectExplorer CMakeProjectManager Debugger QtSupport
  SOURCES
    westbuildstep.cpp westbuildstep.h
    zephyrconstants.h
    zephyrdebug.cpp zephyrdebug.h
    zephyrplugin.cpp
    zephyrproject.cpp zephyrproject.h
    zephyrrun.cpp zephyrrun.h
    zephyrsettings.cpp zephyrsettings.h
    zephyrtr.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Zephyr AND TARGET Qt::Quick)
  qt_add_qml_module(Zephyr
    URI QtCreator.Zephyr
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      ZephyrSettingsPage.qml
  )
endif()

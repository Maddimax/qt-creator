add_qtc_plugin(HarmonyOS
  PLUGIN_DEPENDS Core CMakeProjectManager Debugger ProjectExplorer QtSupport Remote
  SOURCES
    harmonyosbuilddevice.cpp harmonyosbuilddevice.h
    harmonyosconfigurations.cpp harmonyosconfigurations.h
    harmonyosdebugsupport.cpp harmonyosdebugsupport.h
    harmonyosconstants.h
    harmonyosdeploystep.cpp harmonyosdeploystep.h
    harmonyosdevice.cpp harmonyosdevice.h
    harmonyosplugin.cpp
    harmonyosqtversion.cpp harmonyosqtversion.h
    harmonyosrunconfiguration.cpp harmonyosrunconfiguration.h
    harmonyossdk.cpp harmonyossdk.h
    harmonyossettings.cpp harmonyossettings.h
    harmonyostoolchain.cpp harmonyostoolchain.h
    harmonyostr.h
)

extend_qtc_plugin(HarmonyOS
  CONDITION WITH_TESTS
  DEPENDS CmdBridgeClient
  SOURCES
    harmonyosdevice_test.cpp harmonyosdevice_test.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET HarmonyOS)
  qt_add_qml_module(HarmonyOS
    URI QtCreator.HarmonyOS
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      HarmonyOsSettingsPage.qml
  )
endif()

add_qtc_plugin(WebAssembly
  DEPENDS Qt::Core
  PLUGIN_DEPENDS Core ProjectExplorer QtSupport
  SOURCES
    webassembly_global.h
    webassemblyconstants.h
    webassemblydevice.cpp webassemblydevice.h
    webassemblyemsdk.cpp webassemblyemsdk.h
    webassemblyplugin.cpp
    webassemblyqtversion.cpp webassemblyqtversion.h
    webassemblyrunconfiguration.cpp webassemblyrunconfiguration.h
    webassemblysettings.cpp webassemblysettings.h
    webassemblytoolchain.cpp webassemblytoolchain.h
    webassemblytr.h
)

extend_qtc_plugin(WebAssembly
  CONDITION WITH_TESTS
  SOURCES
    webassembly_test.cpp
    webassembly_test.h
)

qtc_add_resources(WebAssembly "images"
  PREFIX "/webassembly"
  BASE "."
  FILES
    images/webassemblydevice.png
    images/webassemblydevice@2x.png
    images/webassemblydevicesmall.png
    images/webassemblydevicesmall@2x.png
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET WebAssembly)
  qt_add_qml_module(WebAssembly
    URI QtCreator.WebAssembly
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      WebAssemblySettingsPage.qml
  )
endif()

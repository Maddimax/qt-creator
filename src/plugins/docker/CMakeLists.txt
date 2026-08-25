add_qtc_plugin(Docker
  DEPENDS Utils CmdBridgeClient
  PLUGIN_DEPENDS Core ProjectExplorer QtSupport
  PLUGIN_TEST_DEPENDS Debugger
  SOURCES
    docker_global.h
    dockertr.h
    dockerapi.cpp dockerapi.h
    dockerconstants.h
    dockercontainerthread.cpp
    dockercontainerthread.h
    dockerdevice.cpp dockerdevice.h
    dockerdeviceenvironmentaspect.cpp dockerdeviceenvironmentaspect.h
    dockerplugin.cpp
    dockersettings.cpp dockersettings.h
)

extend_qtc_plugin(Docker
  CONDITION WITH_TESTS
  SOURCES
    dockerdebuggertest.h
    dockerdebuggertest.cpp
    dockermounttest.h
    dockermounttest.cpp
)

qtc_add_resources(Docker "images"
  PREFIX "/docker"
  BASE "."
  FILES
    images/dockerdevice.png
    images/dockerdevice@2x.png
    images/dockerdevicesmall.png
    images/dockerdevicesmall@2x.png
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Docker AND TARGET Qt::Quick)
  qt_add_qml_module(Docker
    URI QtCreator.Docker
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      DockerSettingsPage.qml
  )
endif()

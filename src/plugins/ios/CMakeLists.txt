add_qtc_plugin(Ios
  DEPENDS QmlDebug Qt::Xml
  PLUGIN_DEPENDS Core Debugger ProjectExplorer QmakeProjectManager CMakeProjectManager
  SOURCES
    devicectlutils.cpp
    devicectlutils.h
    deviceinfo.cpp deviceinfo.h
    ios.qrc
    iosbuildconfiguration.cpp iosbuildconfiguration.h
    iosbuildstep.cpp iosbuildstep.h
    iosconfigurations.cpp iosconfigurations.h
    iosconstants.h
    iosdeploystep.cpp iosdeploystep.h
    iosdevice.cpp iosdevice.h
    iosdsymbuildstep.cpp iosdsymbuildstep.h
    iosplugin.cpp
    iosprobe.cpp iosprobe.h
    iosqtversion.cpp iosqtversion.h
    iosrunconfiguration.cpp iosrunconfiguration.h
    iosrunner.cpp iosrunner.h
    iossettingspage.cpp iossettingspage.h
    iossimulator.cpp iossimulator.h
    iostoolhandler.cpp iostoolhandler.h
    iostr.h
    simulatorcontrol.cpp simulatorcontrol.h
)

extend_qtc_plugin(Ios
  CONDITION APPLE
  DEPENDS ${FWCoreFoundation} ${FWIOKit}
)

# The settings page's Qt Quick form. A module rather than a plain resource so
# that qmllint and qmlcachegen see the file; qbs cannot build QML modules, so it
# only lists it - the same split qtcquick already lives with.
if(TARGET Ios AND TARGET Qt::Quick)
  qt_add_qml_module(Ios
    URI QtCreator.Ios
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      IosSettingsPage.qml
  )
endif()

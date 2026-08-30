add_qtc_plugin(Qnx
  DEPENDS QmlDebug Qt::Xml
  PLUGIN_DEPENDS Core Debugger ProjectExplorer QtSupport Remote
  SOURCES
    qnx.qrc
    qnxanalyzesupport.cpp qnxanalyzesupport.h
    qnxconstants.h
    qnxdebugsupport.cpp qnxdebugsupport.h
    qnxdeployqtlibrariesdialog.cpp qnxdeployqtlibrariesdialog.h
    qnxdevice.cpp qnxdevice.h
    qnxdevicetester.cpp qnxdevicetester.h
    qnxplugin.cpp
    qnxqtversion.cpp qnxqtversion.h
    qnxrunconfiguration.cpp qnxrunconfiguration.h
    qnxtoolchain.cpp qnxtoolchain.h
    qnxtr.h
    qnxutils.cpp qnxutils.h
    slog2inforunner.cpp slog2inforunner.h
)

# The dialog's Qt Quick form. A module rather than a plain resource so that
# qmllint and qmlcachegen see the file; qbs cannot build QML modules, so it
# only lists it - the same split qtcquick already lives with.
if(TARGET Qnx)
  qt_add_qml_module(Qnx
    URI QtCreator.Qnx
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      DeployQtLibrariesDialog.qml
  )
endif()

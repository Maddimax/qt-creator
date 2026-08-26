add_qtc_plugin(QtSupport
  DEPENDS Qt::Xml McpServerLib
  PUBLIC_DEPENDS ProParser
  PLUGIN_DEPENDS Core ProjectExplorer ResourceEditor
  SOURCES
    mcpsupport.cpp mcpsupport.h
    baseqtversion.cpp baseqtversion.h
    codegenerator.cpp codegenerator.h
    codegensettings.cpp codegensettings.h
    exampleslistmodel.cpp exampleslistmodel.h
    examplesparser.cpp
    examplesparser.h
    externaleditors.cpp externaleditors.h
    gettingstartedwelcomepage.cpp gettingstartedwelcomepage.h
    profilereader.cpp profilereader.h
    qscxmlcgenerator.cpp qscxmlcgenerator.h
    qtabiextractor.cpp qtabiextractor.h
    qtbuildaspects.cpp qtbuildaspects.h
    qtcppkitinfo.cpp qtcppkitinfo.h
    qtkitaspect.cpp qtkitaspect.h
    qtoptionspage.cpp qtoptionspage.h
    qtoutputformatter.cpp qtoutputformatter.h
    qtparser.cpp qtparser.h
    qtprojectimporter.cpp qtprojectimporter.h
    qtsupport.qrc
    qtsupport_global.h
    qtsupporttr.h
    qtsupportconstants.h
    qtsupportplugin.cpp
    qtsupportutils.cpp qtsupportutils.h
    qttestparser.cpp qttestparser.h
    qtversionfactory.h
    qtversionmanager.cpp qtversionmanager.h
    qtversions.cpp qtversions.h
    translationwizardpage.cpp translationwizardpage.h
    uicgenerator.cpp uicgenerator.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET QtSupport)
  qt_add_qml_module(QtSupport
    URI QtCreator.QtSupport
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      CodeGenSettingsPage.qml
      QtVersionsPage.qml
  )
endif()

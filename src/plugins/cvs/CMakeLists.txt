add_qtc_plugin(CVS
  PLUGIN_CLASS CvsPlugin
  PLUGIN_DEPENDS Core TextEditor VcsBase
  SOURCES
    cvseditor.cpp
    cvseditor.h
    cvsplugin.cpp
    cvssettings.cpp
    cvssettings.h
    cvssubmiteditor.cpp
    cvssubmiteditor.h
    cvstr.h
    cvsutils.cpp
    cvsutils.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET CVS)
  qt_add_qml_module(CVS
    URI QtCreator.CVS
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      CvsSettingsPage.qml
  )
endif()

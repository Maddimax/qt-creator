add_qtc_plugin(ClearCase
  PLUGIN_DEPENDS Core ProjectExplorer TextEditor VcsBase
  SOURCES
    activityselector.cpp activityselector.h
    annotationhighlighter.cpp annotationhighlighter.h
    checkoutdialog.cpp checkoutdialog.h
    clearcaseconstants.h
    clearcaseeditor.cpp clearcaseeditor.h
    clearcaseplugin.cpp clearcaseplugin.h
    clearcasesettings.cpp clearcasesettings.h
    clearcasesubmiteditor.cpp clearcasesubmiteditor.h
    clearcasesubmiteditorwidget.cpp clearcasesubmiteditorwidget.h
    clearcasesync.cpp clearcasesync.h
    clearcasetr.h
    settingspage.cpp settingspage.h
    versionselector.cpp versionselector.h
)

# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET ClearCase)
  qt_add_qml_module(ClearCase
    URI QtCreator.ClearCase
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      ClearCaseSettingsPage.qml
  )
endif()

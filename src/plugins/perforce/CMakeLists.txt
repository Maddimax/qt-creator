add_qtc_plugin(Perforce
  PLUGIN_DEPENDS Core TextEditor VcsBase
  SOURCES
    annotationhighlighter.cpp annotationhighlighter.h
    changenumberdialog.cpp changenumberdialog.h
    pendingchangesdialog.cpp pendingchangesdialog.h
    perforcechecker.cpp perforcechecker.h
    perforceeditor.cpp perforceeditor.h
    perforceplugin.cpp perforceplugin.h
    perforcesettings.cpp perforcesettings.h
    perforcesubmiteditor.cpp perforcesubmiteditor.h
    perforcesubmiteditorwidget.cpp perforcesubmiteditorwidget.h
    perforcetr.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Perforce)
  qt_add_qml_module(Perforce
    URI QtCreator.Perforce
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      PerforceSettingsPage.qml
  )
endif()

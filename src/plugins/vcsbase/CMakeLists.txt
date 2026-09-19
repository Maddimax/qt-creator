add_qtc_plugin(VcsBase
  PLUGIN_CLASS VcsPlugin
  PLUGIN_DEPENDS Core DiffEditor ProjectExplorer TextEditor
  PLUGIN_RECOMMENDS CodePaster CppEditor
  SOURCES
    baseannotationhighlighter.cpp baseannotationhighlighter.h
    cleandialog.cpp cleandialog.h
    commonvcssettings.cpp commonvcssettings.h
    diffandloghighlighter.cpp diffandloghighlighter.h
    nicknamedialog.cpp nicknamedialog.h
    submiteditorfile.cpp submiteditorfile.h
    submiteditorwidget.cpp submiteditorwidget.h
    submitfieldwidget.cpp submitfieldwidget.h
    submitfilemodel.cpp submitfilemodel.h
    vcsbase.qrc
    vcsbase_global.h
    vcsbasetr.h
    vcsbaseclient.cpp vcsbaseclient.h
    vcsbaseclientsettings.cpp vcsbaseclientsettings.h
    vcsbaseconstants.h
    vcsbasediffeditorcontroller.cpp vcsbasediffeditorcontroller.h
    vcsbaseeditor.cpp vcsbaseeditor.h
    vcsbaseeditorconfig.cpp vcsbaseeditorconfig.h
    vcseditordocument.cpp vcseditordocument.h
    vcsbaseplugin.cpp vcsbaseplugin.h
    vcsbasesubmiteditor.cpp vcsbasesubmiteditor.h
    vcschangesview.cpp vcschangesview.h
    vcscommand.cpp vcscommand.h
    vcsenums.h
    vcsfilestatus.h
    vcsoutputformatter.cpp vcsoutputformatter.h
    vcsoutputwindow.cpp vcsoutputwindow.h
    vcsplugin.cpp vcsplugin.h
    wizard/vcscommandpage.cpp wizard/vcscommandpage.h
    wizard/vcsconfigurationpage.cpp wizard/vcsconfigurationpage.h
    wizard/vcsjsextension.cpp wizard/vcsjsextension.h
)

extend_qtc_plugin(VcsBase
  CONDITION WITH_TESTS
  DEFINES SRC_DIR="${PROJECT_SOURCE_DIR}"
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET VcsBase)
  qt_add_qml_module(VcsBase
    URI QtCreator.VcsBase
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      CleanDialog.qml
      CommonVcsSettingsPage.qml
      NickNameDialog.qml
  )
endif()

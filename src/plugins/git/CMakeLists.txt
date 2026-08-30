add_qtc_plugin(Git
  DEPENDS McpServerLib
  PLUGIN_DEPENDS Core DiffEditor TextEditor VcsBase
  SOURCES
    annotationhighlighter.cpp annotationhighlighter.h
    branchadddialog.cpp branchadddialog.h
    branchcheckoutdialog.cpp branchcheckoutdialog.h
    branchmodel.cpp branchmodel.h
    branchview.cpp branchview.h
    changeselectiondialog.cpp changeselectiondialog.h
    commitdata.cpp commitdata.h
    gerrit/authenticationdialog.cpp gerrit/authenticationdialog.h
    gerrit/gerritdialog.cpp gerrit/gerritdialog.h
    gerrit/gerritmodel.cpp gerrit/gerritmodel.h
    gerrit/gerritoptionspage.cpp gerrit/gerritoptionspage.h
    gerrit/gerritparameters.cpp gerrit/gerritparameters.h
    gerrit/gerritplugin.cpp gerrit/gerritplugin.h
    gerrit/gerritpushdialog.cpp gerrit/gerritpushdialog.h
    gerrit/gerritremotechooser.cpp gerrit/gerritremotechooser.h
    gerrit/gerritserver.cpp gerrit/gerritserver.h
    git.qrc
    git_global.h
    gittr.h
    gitclient.cpp gitclient.h
    gitconstants.h
    giteditor.cpp giteditor.h
    gitgraphmodel.cpp gitgraphmodel.h
    gitgraphview.cpp gitgraphview.h
    gitgrep.cpp gitgrep.h
    githighlighters.cpp githighlighters.h
    gitplugin.cpp gitplugin.h
    gitsettings.cpp gitsettings.h
    gitsubmiteditor.cpp gitsubmiteditor.h
    gitsubmiteditorwidget.cpp gitsubmiteditorwidget.h
    gitutils.cpp gitutils.h
    instantblame.cpp instantblame.h
    logchangedialog.cpp logchangedialog.h
    mcpsupport.cpp mcpsupport.h
    mergetool.cpp mergetool.h
    remotedialog.cpp remotedialog.h
    remotemodel.cpp remotemodel.h
    stashdialog.cpp stashdialog.h
    temporarypatchfile.cpp temporarypatchfile.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Git)
  qt_add_qml_module(Git
    URI QtCreator.Git
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      BranchCheckoutDialog.qml
      BranchAddDialog.qml
      GitSettingsPage.qml
      GerritDialog.qml
      GerritPushDialog.qml
      LogChangeDialog.qml
      StashDialog.qml
      gerrit/GerritSettingsPage.qml
  )
endif()

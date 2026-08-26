add_qtc_plugin(GitLab
  PLUGIN_CLASS GitLabPlugin
  PLUGIN_DEPENDS Core ProjectExplorer Git VcsBase
  DEPENDS Utils
  SOURCES
    gitlabclonedialog.cpp gitlabclonedialog.h
    gitlabdialog.cpp gitlabdialog.h
    gitlaboptionspage.cpp gitlaboptionspage.h
    gitlabparameters.cpp gitlabparameters.h
    gitlabplugin.cpp gitlabplugin.h
    gitlabprojectsettings.cpp gitlabprojectsettings.h
    gitlabquery.cpp gitlabquery.h
    gitlabtr.h
    resultparser.cpp resultparser.h
)

extend_qtc_plugin(GitLab
  CONDITION WITH_TESTS
  DEPENDS Qt::Test
)

# The settings page's Qt Quick form. A module rather than a plain resource so
# that qmllint and qmlcachegen see the file; qbs cannot build QML modules, so
# it only lists it - the same split qtcquick already lives with.
if(TARGET GitLab)
  qt_add_qml_module(GitLab
    URI QtCreator.GitLab
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      GitLabProjectPanel.qml
      GitLabSettingsPage.qml
  )
endif()

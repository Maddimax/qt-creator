add_qtc_plugin(Coco
  PLUGIN_DEPENDS
    QtCreator::Core
    QtCreator::LanguageClient
    QtCreator::ProjectExplorer
  DEPENDS
    QtCreator::ExtensionSystem
  SOURCES
    Coco.json.in
    buildsettings.cpp
    buildsettings.h
    cmakemodificationfile.cpp
    cmakemodificationfile.h
    cocobuildstep.cpp
    cocobuildstep.h
    cococommon.cpp
    cococommon.h
    cocolanguageclient.cpp
    cocolanguageclient.h
    cocoplugin.cpp
    cocoplugin.qrc
    cocoplugin_global.h
    cocopluginconstants.h
    cocoprojectwidget.cpp
    cocoprojectwidget.h
    cocotr.h
    files/cocoplugin-clang.cmake
    files/cocoplugin-gcc.cmake
    files/cocoplugin-visualstudio.cmake
    files/cocoplugin.cmake
    files/cocoplugin.prf
    globalsettings.cpp
    globalsettings.h
    images/SquishCoco_48x48.png
    modificationfile.cpp
    modificationfile.h
    qmakefeaturefile.cpp
    qmakefeaturefile.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Coco)
  qt_add_qml_module(Coco
    URI QtCreator.Coco
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      CocoBuildStep.qml
      CocoSettingsPage.qml
  )
endif()

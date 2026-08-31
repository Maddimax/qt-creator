find_package(Qt6 COMPONENTS SvgWidgets QUIET)
if (TARGET Qt::SvgWidgets)
  set(SVG_WIDGETS Qt::SvgWidgets)
endif()

add_qtc_plugin(ImageViewer
  DEPENDS OptionalSvg ${SVG_WIDGETS}
  PLUGIN_DEPENDS Core
  SOURCES
    exportdialog.cpp exportdialog.h
    imageview.cpp imageview.h
    imageviewer.cpp imageviewer.h
    imageviewerconstants.h
    imageviewerfile.cpp imageviewerfile.h
    imageviewerplugin.cpp
    imageviewertr.h
    multiexportdialog.cpp multiexportdialog.h
)

# The dialogs' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET ImageViewer)
  qt_add_qml_module(ImageViewer
    URI QtCreator.ImageViewer
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      ExportDialog.qml
      MultiExportDialog.qml
  )
endif()

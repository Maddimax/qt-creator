add_subdirectory(style)

add_qtc_library(QtcQuick
  CONDITION TARGET Qt::Quick AND TARGET Qt::QuickWidgets AND TARGET Qt::QuickControls2
  DEPENDS QtcQuickStyle
  PUBLIC_DEPENDS
    Utils
    Qt::Gui
    Qt::Qml
    Qt::Quick
    Qt::QuickControls2
    Qt::QuickWidgets
    Qt::Widgets
  SOURCES
    qtcdesignsystem.cpp qtcdesignsystem.h
    qtcquick_global.h
    qtcquickengine.cpp qtcquickengine.h
    qtcquickwidget.cpp qtcquickwidget.h
)

if(TARGET QtcQuick)
  set_source_files_properties(Fonts.qml Tokens.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

  qt_add_qml_module(QtcQuick
    URI QtCreator.Ui
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      Fonts.qml
      Tokens.qml
  )
endif()

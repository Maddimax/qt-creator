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
    aspectcontainermodel.cpp aspectcontainermodel.h
    aspectform.cpp aspectform.h
    qtcdesignsystem.cpp qtcdesignsystem.h
    qtciconprovider.cpp qtciconprovider.h
    qtcquick_global.h
    qtctokens.cpp qtctokens.h
    qtcquickengine.cpp qtcquickengine.h
    qtcquickwidget.cpp qtcquickwidget.h
)

if(TARGET QtcQuick)
  set_source_files_properties(Metrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

  qt_add_qml_module(QtcQuick
    URI QtCreator.Ui
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      AspectForm.qml
      BoolDelegate.qml
      ColorDelegate.qml
      DoubleDelegate.qml
      FilePathListDelegate.qml
      FontFamilyDelegate.qml
      IntegerDelegate.qml
      Metrics.qml
      MultiSelectionDelegate.qml
      SelectionDelegate.qml
      StringDelegate.qml
      StringListDelegate.qml
      TextDisplayDelegate.qml
      UnsupportedDelegate.qml
  )
endif()

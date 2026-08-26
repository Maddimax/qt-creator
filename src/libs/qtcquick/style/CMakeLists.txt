add_qtc_library(QtcQuickStyle
  DEPENDS Utils
  PUBLIC_DEPENDS
    Qt::Qml
    Qt::Quick
    Qt::QuickControls2
  SOURCES
    qtcquickstyle.cpp
)

if(TARGET QtcQuickStyle)
  qt_add_qml_module(QtcQuickStyle
    URI QtCreatorStyle
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      Button.qml
      CheckBox.qml
      ComboBox.qml
      GroupBox.qml
      ItemDelegate.qml
      Label.qml
      RadioButton.qml
      ScrollBar.qml
      ScrollView.qml
      SpinBox.qml
      Switch.qml
      TextField.qml
      ToolTip.qml
  )
endif()

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
    aspectitemlistmodel.cpp aspectitemlistmodel.h
    aspectmodels.cpp aspectmodels.h
    aspectform.cpp aspectform.h
    namedaspects.cpp namedaspects.h
    qtcdesignsystem.cpp qtcdesignsystem.h
    qtciconprovider.cpp qtciconprovider.h
    qtcquick_global.h
    qtctokens.cpp qtctokens.h
    tablefiltermodel.cpp tablefiltermodel.h
    treefiltermodel.cpp treefiltermodel.h
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
      AspectGroupBox.qml
      AspectItems.qml
      AspectListDelegate.qml
      AspectTableCell.qml
      AspectPage.qml
      BoolDelegate.qml
      ButtonDelegate.qml
      ColorDelegate.qml
      CompletionPopup.qml
      DoubleDelegate.qml
      FilePathListDelegate.qml
      FontFamilyDelegate.qml
      GroupDelegate.qml
      GroupedListDelegate.qml
      IntegerDelegate.qml
      Metrics.qml
      MultiSelectionDelegate.qml
      QtcBadge.qml
      QtcButton.qml
      QtcIconDisplay.qml
      QtcLabel.qml
      QtcLineEdit.qml
      QtcPageIndicator.qml
      QtcProgressBar.qml
      QtcSearchBox.qml
      RadioDelegate.qml
      RadioGroupDelegate.qml
      QtcSwitch.qml
      SelectionDelegate.qml
      SecretDelegate.qml
      StringDelegate.qml
      StringListDelegate.qml
      StringListEditorDelegate.qml
      TableDelegate.qml
      TextAreaDelegate.qml
      TreeDelegate.qml
      TextDisplayDelegate.qml
      TextWithActionDelegate.qml
      UnsupportedDelegate.qml
  )
endif()

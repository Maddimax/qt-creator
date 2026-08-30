add_subdirectory(style)

add_qtc_library(QtcQuick
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
    actionmodel.cpp actionmodel.h
    aspectcontainermodel.cpp aspectcontainermodel.h
    aspectitemlistmodel.cpp aspectitemlistmodel.h
    aspectmodels.cpp aspectmodels.h
    aspectform.cpp aspectform.h
    filebrowser.cpp filebrowser.h
    namedaspects.cpp namedaspects.h
    qtcdesignsystem.cpp qtcdesignsystem.h
    qtciconprovider.cpp qtciconprovider.h
    qtcquick_global.h
    qtctokens.cpp qtctokens.h
    tablefiltermodel.cpp tablefiltermodel.h
    treefiltermodel.cpp treefiltermodel.h
    qtcquickengine.cpp qtcquickengine.h
    outputview.cpp outputview.h
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
      AspectContextMenu.qml
      AspectItems.qml
      AspectInlineListDelegate.qml
      AspectListDelegate.qml
      AspectTableCell.qml
      AspectPage.qml
      BoolDelegate.qml
      BoolWithOwnLabelDelegate.qml
      ButtonDelegate.qml
      ColorDelegate.qml
      CompletionPopup.qml
      QtcVariableChooser.qml
      DeviceBrowse.qml
      DoubleDelegate.qml
      EnvironmentEditor.qml
      EnvironmentEditorDelegate.qml
      FilePathListDelegate.qml
      FontFamilyDelegate.qml
      FontPickerDelegate.qml
      FormLabel.qml
      GroupDelegate.qml
      InlineGroupDelegate.qml
      FlattenedGroupDelegate.qml
      GroupedListDelegate.qml
      IntegerDelegate.qml
      KeySequenceDelegate.qml
      Metrics.qml
      OutputView.qml
      MultiSelectionDelegate.qml
      QtcFileDialog.qml
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
      SidebarRow.qml
      StringDelegate.qml
      StringListDelegate.qml
      StringListEditorDelegate.qml
      TableDelegate.qml
      TextAreaDelegate.qml
      TreeDelegate.qml
      TextDisplayDelegate.qml
      TextWithActionDelegate.qml
      TriStateDelegate.qml
      UnsupportedDelegate.qml
  )
endif()

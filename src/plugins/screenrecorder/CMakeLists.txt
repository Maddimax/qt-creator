add_qtc_plugin(ScreenRecorder
  PLUGIN_DEPENDS Core
  DEPENDS Utils QtcQuick
  SOURCES
    cropandtrim.cpp cropandtrim.h
    cropscene.cpp cropscene.h
    export.cpp export.h
    ffmpegutils.cpp ffmpegutils.h
    record.cpp record.h
    screenrecorderconstants.h
    screenrecorderplugin.cpp
    screenrecordersettings.cpp screenrecordersettings.h
    screenrecordertr.h
)

extend_qtc_plugin(ScreenRecorder
  CONDITION WITH_TESTS
  SOURCES
    screenrecorder_test.cpp screenrecorder_test.h
  EXPLICIT_MOC screenrecorder_test.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET ScreenRecorder)
  qt_add_qml_module(ScreenRecorder
    URI QtCreator.ScreenRecorder
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      CropPage.qml
      CropScene.qml
      RecordOptionsPage.qml
      TrimPage.qml
      TrimSlider.qml
      ScreenRecorderSettingsPage.qml
  )
endif()
